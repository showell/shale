// builtins_store.c: the store's builtins that look and read, Zig Shale's
// src/script/builtins_store.zig (`ls`, `stat`, `exists`, `is_dir` and what
// every store builtin shares: a path confined, a store failure's stop, a read
// counted) and builtins_read.zig (a file whole or in parts, its lines, the
// lines holding a literal, and scans that count or find without holding the
// file). Every path goes through path_run.c's `path_arg`, every read is
// counted against the read budgets where Zig counts it, and every message is
// Zig's. The walk is builtins_walk.c, the changes builtins_change.c.
//
// A program includes store.c and path_run.c, then this after builtins.c and
// builtins_text.c.

// One change the script attempted, in order, Zig's `interp.Effect`: the
// builtin's name, the script's path, and "ok" or the store's error.
typedef struct {
    Str op;
    Str path;
    Str outcome;
} Effect;

VEC_DEFINE(Effects, Effect)

// What a run's store builtins need beyond the run itself: the store, the
// path checks' context (the root, the run's charge, the host's hook), the
// effects log, the scans' buffer and how a change's diff is printed.
typedef struct RunStore {
    Store store;
    PathRun path;
    // In the values' arena: a yield copies the log (vm.c's `collect_run`).
    Effects effects;
    // The buffer every scan reuses, in the values' arena; a yield drops it.
    Bytes scan_buf;
    // A change's diff as one line, the CLI's way unless `--verbose`.
    bool diffs_summary;
} RunStore;

// Each of the effects as the report prints it, `op path: outcome`, into
// `*out`.
// DRAGON: the secrets rule. A report line that named a secrets mount's
// folder would print where the tokens are.
// REMEDY: a change's path was confined, and refused under a secrets folder,
// before the change was made; each line is put to the hook's one check here
// all the same, and one that names such a folder ends the process.
static STATUS_MUST_USE Status store_effect_lines(RunStore *rs, Arena *a, Names *out) {
    Effects e = rs->effects;
    Str *lines = ARENA_ARRAY(a, Str, e.len);
    if (e.len > 0 && lines == NULL) return STOP;
    SLICE_TYPE(Lines, Str);
    Lines all = {lines, e.len};
    const PathHook *hook = rs->path.hook;
    for (size_t k = 0; k < e.len; k++) {
        Buf b = buf_make(a);
        // The line's room taken first, so each piece is copied into it.
        TRY(buf_reserve(&b, at(e, k).op.len + at(e, k).path.len + at(e, k).outcome.len + 3));
        TRY(buf_str(&b, at(e, k).op));
        TRY(buf_byte(&b, ' '));
        TRY(buf_str(&b, at(e, k).path));
        TRY(buf_lit(&b, ": "));
        TRY(buf_str(&b, at(e, k).outcome));
        bool names = false;
        Pos none = {0, 0};
        if (hook != NULL && hook->names_secret(hook->ctx, &rs->path, none, buf_view(&b), &names) != OK) return STOP;
        if (names) {
            fprintf(stderr, "builtins_store: a report line names a secrets mount's folder\n");
            abort();
        }
        at(all, k) = buf_view(&b);
    }
    out->ptr = lines;
    out->len = e.len;
    return OK;
}

// The run's store; a store builtin on a run made with none is a bug in the C.
static RunStore *builtin_store_of(Run *r) {
    if (r->store == NULL) {
        fprintf(stderr, "builtins_store: a store builtin ran on a run with no store\n");
        abort();
    }
    return r->store;
}

// The run's step charge as path_run.c takes it, Zig's `Run.charge`.
static STATUS_MUST_USE Status builtin_path_charge(void *vm, RunCtx *ctx, Pos pos, uint64_t n) {
    (void)ctx;
    return run_charge(vm, pos, n);
}

// `v`, the path builtin `b` was given, confined: Zig's `pathArg`, its string
// checked first.
static STATUS_MUST_USE Status builtin_path(Run *r, Pos pos, Builtin b, const Value *v, bool allow_root, StorePath *out) {
    Str given = {0};
    TRY(builtin_string_arg(r, pos, b, v, S("its path"), &given));
    return path_arg(&builtin_store_of(r)->path, pos, builtin_who(r, b), given, allow_root, out);
}

// Zig's `storeFailed`: the stop for builtin `b`'s store call on `rel`.
static STATUS_MUST_USE Status builtin_store_failed(Run *r, Pos pos, Builtin b, Str rel, StoreErr e) {
    return path_store_failed(&builtin_store_of(r)->path, pos, builtin_who(r, b), rel, e);
}

// Zig's `countRead`: one store read, against the reads budget.
static STATUS_MUST_USE Status builtin_count_read(Run *r, Pos pos) {
    if (r->reads >= r->ctx.budgets.reads) return run_budget_stop(&r->ctx, pos, BUDGET_READS);
    r->reads++;
    return OK;
}

static Str builtin_kind_name(StoreKind k) {
    if (k == STORE_KIND_FILE) return S("file");
    if (k == STORE_KIND_DIRECTORY) return S("directory");
    return S("other");
}

// `p`'s stat, `*found` false when nothing is there; a failure is `b`'s stop.
static STATUS_MUST_USE Status builtin_stat_or_null(Run *r, Pos pos, Builtin b, const StorePath *p, bool *found, StoreStat *st) {
    RunStore *rs = builtin_store_of(r);
    StoreErr e = rs->store.stat_or_null(rs->store.ctx, r->ctx.values, p->full, found, st);
    if (e != STORE_OK) return builtin_store_failed(r, pos, b, p->rel, e);
    return OK;
}

// Whether `a` sorts before `b` by their bytes, Zig's `std.mem.lessThan`.
static bool builtin_bytes_less(Str a, Str b) {
    size_t n = a.len < b.len ? a.len : b.len;
    int c = n > 0 ? memcmp(a.ptr, b.ptr, n) : 0;
    return c < 0 || (c == 0 && a.len < b.len);
}

static int builtin_entry_order(const void *x, const void *y) {
    const StoreEntry *a = x;
    const StoreEntry *b = y;
    if (builtin_bytes_less(a->name, b->name)) return -1;
    return builtin_bytes_less(b->name, a->name) ? 1 : 0;
}

// A folder's entries sorted by name, Zig's `sortedEntries`, charged as `ls`
// is: a step an entry and the sort's bound. The caller counts the read. A
// folder's names are distinct, so any sort gives Zig's order.
static STATUS_MUST_USE Status builtin_sorted_entries(Run *r, Pos pos, Builtin b, Str full, Str rel, StoreEntries *out) {
    RunStore *rs = builtin_store_of(r);
    StoreErr e = rs->store.list(rs->store.ctx, r->ctx.values, full, out);
    if (e != STORE_OK) return builtin_store_failed(r, pos, b, rel, e);
    uint64_t name_bytes = 0;
    for (size_t i = 0; i < out->len; i++) name_bytes += at(*out, i).name.len;
    TRY(run_charge(r, pos, out->len + builtin_sort_bound(out->len, name_bytes)));
    if (out->len > 1) qsort(out->ptr, out->len, sizeof(StoreEntry), builtin_entry_order);
    return OK;
}

// ── `ls`, `stat`, `exists`, `is_dir` ──

// `ls(p)`: the names in a folder, sorted. The store lists a missing folder as
// empty; a script's `ls` of one is a stop.
static STATUS_MUST_USE Status bi_ls(Run *r, Pos pos, ConstValues args, Value *out) {
    StorePath p;
    TRY(builtin_path(r, pos, BUILTIN_LS, &at(args, 0), true, &p));
    TRY(builtin_count_read(r, pos));
    bool found = false;
    StoreStat st;
    TRY(builtin_stat_or_null(r, pos, BUILTIN_LS, &p, &found, &st));
    if (!found) return builtin_store_failed(r, pos, BUILTIN_LS, p.rel, STORE_FILE_NOT_FOUND);
    if (st.kind != STORE_KIND_DIRECTORY) return builtin_store_failed(r, pos, BUILTIN_LS, p.rel, STORE_NOT_DIR);
    StoreEntries entries;
    TRY(builtin_sorted_entries(r, pos, BUILTIN_LS, p.full, p.rel, &entries));
    Table *t = NULL;
    TRY(builtin_new_list(r, pos, &t));
    if (ValueVec_reserve(&t->list, entries.len) != OK) return run_values_oom(r, pos);
    for (size_t i = 0; i < entries.len; i++) TRY(builtin_push_str(r, pos, t, at(entries, i).name));
    value_table(out, t);
    return OK;
}

// `stat(p)`: `{size, kind, modified}`, `modified` in whole seconds.
static STATUS_MUST_USE Status bi_stat(Run *r, Pos pos, ConstValues args, Value *out) {
    StorePath p;
    TRY(builtin_path(r, pos, BUILTIN_STAT, &at(args, 0), true, &p));
    TRY(builtin_count_read(r, pos));
    RunStore *rs = builtin_store_of(r);
    StoreStat st;
    StoreErr e = rs->store.stat(rs->store.ctx, r->ctx.values, p.full, &st);
    if (e != STORE_OK) return builtin_store_failed(r, pos, BUILTIN_STAT, p.rel, e);
    Table *t = NULL;
    TRY(run_new_table(r, pos, &t));
    TRY(run_map_put_int(r, pos, t, S("size"), (int64_t)st.size));
    TRY(run_map_put_str(r, pos, t, S("kind"), builtin_kind_name(st.kind)));
    TRY(run_map_put_int(r, pos, t, S("modified"), st.mtime_s));
    t->kind = TABLE_MAP;
    value_table(out, t);
    return OK;
}

static STATUS_MUST_USE Status bi_exists(Run *r, Pos pos, ConstValues args, Value *out) {
    StorePath p;
    TRY(builtin_path(r, pos, BUILTIN_EXISTS, &at(args, 0), true, &p));
    TRY(builtin_count_read(r, pos));
    RunStore *rs = builtin_store_of(r);
    bool has = false;
    StoreErr e = rs->store.has(rs->store.ctx, r->ctx.values, p.full, &has);
    if (e != STORE_OK) return builtin_store_failed(r, pos, BUILTIN_EXISTS, p.rel, e);
    value_bool(out, has);
    return OK;
}

static STATUS_MUST_USE Status bi_is_dir(Run *r, Pos pos, ConstValues args, Value *out) {
    StorePath p;
    TRY(builtin_path(r, pos, BUILTIN_IS_DIR, &at(args, 0), true, &p));
    TRY(builtin_count_read(r, pos));
    bool found = false;
    StoreStat st;
    TRY(builtin_stat_or_null(r, pos, BUILTIN_IS_DIR, &p, &found, &st));
    value_bool(out, found && st.kind == STORE_KIND_DIRECTORY);
    return OK;
}

// ── reading files ──

// The size of the file at `p`, Zig's `fileSize`: a stop when it is missing
// or not a file. One store call, counted as part of the read.
static STATUS_MUST_USE Status builtin_file_size(Run *r, Pos pos, Builtin b, const StorePath *p, uint64_t *size) {
    bool found = false;
    StoreStat st;
    TRY(builtin_stat_or_null(r, pos, b, p, &found, &st));
    if (!found) return builtin_store_failed(r, pos, b, p->rel, STORE_FILE_NOT_FOUND);
    if (st.kind != STORE_KIND_FILE) return builtin_store_failed(r, pos, b, p->rel, STORE_IS_DIR);
    *size = st.size;
    return OK;
}

// `n` bytes from `offset` (to the end when `!has_n`) of a file whose size is
// known, Zig's `readSized`: "" at or past the end; at most the call-bytes cap
// and what the read-bytes budget has left; the bytes read charged a step per
// 16.
static STATUS_MUST_USE Status builtin_read_sized(Run *r, Pos pos, Builtin b, const StorePath *p, uint64_t size, uint64_t offset, bool has_n, uint64_t n, Str *out) {
    *out = S("");
    if (offset >= size) return OK;
    uint64_t left = size - offset;
    uint64_t want = has_n && n < left ? n : left;
    uint64_t cap = r->ctx.budgets.call_bytes;
    if (want > cap) {
        return run_failf(r, pos, CLASS_ARGUMENT, "`{s}` brings at most {d} bytes in one call, and this would be {d}: read it in parts, `read(p, offset, n)`",
                         S("`"), builtin_who(r, b), S("` brings at most "), run_unum(r, cap), S(" bytes in one call, and this would be "), run_unum(r, want), S(": read it in parts, `read(p, offset, n)`"));
    }
    if (want > run_sat_sub(r->ctx.budgets.read_bytes, r->read_bytes)) return run_budget_stop(&r->ctx, pos, BUDGET_READ_BYTES);
    char *buf = arena_alloc(r->ctx.values, (size_t)want, 1);
    if (buf == NULL) return run_values_oom(r, pos);
    RunStore *rs = builtin_store_of(r);
    Bytes room = {buf, (size_t)want};
    size_t got = 0;
    StoreErr e = rs->store.read_at(rs->store.ctx, r->ctx.values, p->full, offset, room, &got);
    if (e != STORE_OK) return builtin_store_failed(r, pos, b, p->rel, e);
    r->read_bytes += got;
    TRY(builtin_charge_bytes(r, pos, got));
    out->ptr = buf;
    out->len = got;
    return OK;
}

// An offset into a file, which must not be negative: "`<who>`: offset N is
// before the file's start".
static STATUS_MUST_USE Status builtin_offset_arg(Run *r, Pos pos, Builtin b, const Value *v, uint64_t *out) {
    int64_t o = 0;
    TRY(builtin_int_arg(r, pos, b, v, S("its offset"), &o));
    if (o < 0) {
        return run_failf(r, pos, CLASS_ARGUMENT, "`{s}`: offset {d} is before the file's start", S("`"), builtin_who(r, b), S("`: offset "), run_num(r, o), S(" is before the file's start"));
    }
    *out = (uint64_t)o;
    return OK;
}

// A count of bytes that must not be negative: "`<who>`: a count of N bytes".
static STATUS_MUST_USE Status builtin_count_arg(Run *r, Pos pos, Builtin b, const Value *v, uint64_t *out) {
    int64_t n = 0;
    TRY(builtin_int_arg(r, pos, b, v, S("its count of bytes"), &n));
    if (n < 0) return run_failf(r, pos, CLASS_ARGUMENT, "`{s}`: a count of {d} bytes", S("`"), builtin_who(r, b), S("`: a count of "), run_num(r, n), S(" bytes"));
    *out = (uint64_t)n;
    return OK;
}

// `read(p)` gives the whole file; `read(p, offset[, n])` `n` bytes from the
// 0-based byte `offset`, or to the end, "" at or past the end.
static STATUS_MUST_USE Status bi_read(Run *r, Pos pos, ConstValues args, Value *out) {
    StorePath p;
    TRY(builtin_path(r, pos, BUILTIN_READ, &at(args, 0), false, &p));
    uint64_t offset = 0;
    bool has_n = false;
    uint64_t n = 0;
    if (args.len > 1) {
        TRY(builtin_offset_arg(r, pos, BUILTIN_READ, &at(args, 1), &offset));
        if (args.len > 2) {
            TRY(builtin_count_arg(r, pos, BUILTIN_READ, &at(args, 2), &n));
            has_n = true;
        }
    }
    TRY(builtin_count_read(r, pos));
    uint64_t size = 0;
    TRY(builtin_file_size(r, pos, BUILTIN_READ, &p, &size));
    Str got = {0};
    TRY(builtin_read_sized(r, pos, BUILTIN_READ, &p, size, offset, has_n, n, &got));
    value_string(out, got);
    return OK;
}

// `read_or(p, default)`: the whole file, or `default` when nothing is there,
// in one store read; a folder or a failure is the store's stop, as `read`'s.
static STATUS_MUST_USE Status bi_read_or(Run *r, Pos pos, ConstValues args, Value *out) {
    StorePath p;
    TRY(builtin_path(r, pos, BUILTIN_READ_OR, &at(args, 0), false, &p));
    TRY(builtin_count_read(r, pos));
    bool found = false;
    StoreStat st;
    TRY(builtin_stat_or_null(r, pos, BUILTIN_READ_OR, &p, &found, &st));
    if (!found) {
        *out = at(args, 1);
        return OK;
    }
    if (st.kind != STORE_KIND_FILE) return builtin_store_failed(r, pos, BUILTIN_READ_OR, p.rel, STORE_IS_DIR);
    Str got = {0};
    TRY(builtin_read_sized(r, pos, BUILTIN_READ_OR, &p, st.size, 0, false, 0, &got));
    value_string(out, got);
    return OK;
}

// `read_lines`'s default part, cut to the call-bytes cap, and how much
// `read_lines` and `line_at` read at a time past it to find a line's end or
// start.
enum {
    BUILTIN_READ_LINES_PART = 64 * 1024,
    BUILTIN_LINE_PIECE = 512,
};

static STATUS_MUST_USE Status builtin_line_too_long(Run *r, Pos pos, Builtin b, uint64_t at_, uint64_t cap) {
    return run_failf(r, pos, CLASS_ARGUMENT, "`{s}`: the line at offset {d} runs past {d} bytes, the most one call brings", S("`"), builtin_who(r, b), S("`: the line at offset "), run_unum(r, at_),
                     S(" runs past "), run_unum(r, cap), S(" bytes, the most one call brings"));
}

static uint64_t builtin_min_u64(uint64_t a, uint64_t b) {
    return a < b ? a : b;
}

// `read_lines(p[, offset[, n]])`: whole lines from the byte `offset`, at
// least `n` bytes (64 KiB unset) and on to the end of the line it stops in,
// as `{lines, next, done}`. Zig's `readLines`.
static STATUS_MUST_USE Status bi_read_lines(Run *r, Pos pos, ConstValues args, Value *out) {
    Builtin b = BUILTIN_READ_LINES;
    StorePath p;
    TRY(builtin_path(r, pos, b, &at(args, 0), false, &p));
    uint64_t offset = 0;
    if (args.len > 1) TRY(builtin_offset_arg(r, pos, b, &at(args, 1), &offset));
    uint64_t cap = r->ctx.budgets.call_bytes;
    uint64_t n = builtin_min_u64(BUILTIN_READ_LINES_PART, cap);
    if (args.len > 2) {
        int64_t x = 0;
        TRY(builtin_int_arg(r, pos, b, &at(args, 2), S("its count of bytes"), &x));
        if (x < 1) return run_failf(r, pos, CLASS_ARGUMENT, "`read_lines`: a part of {d} bytes: it brings at least 1", S("`read_lines`: a part of "), run_num(r, x), S(" bytes: it brings at least 1"));
        n = (uint64_t)x;
    }
    if (n > cap) {
        return run_failf(r, pos, CLASS_ARGUMENT, "`read_lines` brings at most {d} bytes in one call, and this asks {d}: a smaller part, and `next` to go on", S("`read_lines` brings at most "), run_unum(r, cap),
                         S(" bytes in one call, and this asks "), run_unum(r, n), S(": a smaller part, and `next` to go on"));
    }
    TRY(builtin_count_read(r, pos));
    uint64_t size = 0;
    TRY(builtin_file_size(r, pos, b, &p, &size));
    uint64_t start = builtin_min_u64(offset, size);
    Buf got = buf_make(r->ctx.values);
    Str first = {0};
    TRY(builtin_read_sized(r, pos, b, &p, size, start, true, n, &first));
    if (buf_str(&got, first) != OK) return run_values_oom(r, pos);
    uint64_t end = start + got.len;
    // Extend a piece at a time to the end of the last line.
    if (end < size && at(got, got.len - 1) != '\n') {
        for (;;) {
            if (end - start >= cap) return builtin_line_too_long(r, pos, b, start, cap);
            Str piece = {0};
            TRY(builtin_read_sized(r, pos, b, &p, size, end, true, builtin_min_u64(BUILTIN_LINE_PIECE, cap - (end - start)), &piece));
            if (piece.len == 0) break;
            const char *nl = memchr(piece.ptr, '\n', piece.len);
            if (nl != NULL) {
                size_t i = (size_t)(nl - piece.ptr);
                if (buf_str(&got, str_sub(piece, 0, i + 1)) != OK) return run_values_oom(r, pos);
                end += i + 1;
                break;
            }
            if (buf_str(&got, piece) != OK) return run_values_oom(r, pos);
            end += piece.len;
            if (end == size) break;
        }
    }
    Table *t = NULL;
    TRY(run_new_table(r, pos, &t));
    Table *list = NULL;
    TRY(builtin_new_list(r, pos, &list));
    TRY(run_charge(r, pos, got.len));
    Str rest = buf_view(&got);
    while (rest.len > 0) {
        const char *nl = memchr(rest.ptr, '\n', rest.len);
        size_t i = nl != NULL ? (size_t)(nl - rest.ptr) : rest.len;
        TRY(builtin_push_str(r, pos, list, str_sub(rest, 0, i)));
        rest = nl != NULL ? str_sub(rest, i + 1, rest.len) : S("");
    }
    t->kind = TABLE_MAP;
    Value lv;
    value_table(&lv, list);
    TRY(run_map_put(r, pos, t, S("lines"), &lv));
    TRY(run_map_put_int(r, pos, t, S("next"), (int64_t)end));
    TRY(run_map_put_bool(r, pos, t, S("done"), end >= size));
    value_table(out, t);
    return OK;
}

// The byte after the last newline in `s` before `to`, or 0.
static size_t builtin_line_start(Str s, size_t to) {
    size_t i = to;
    while (i > 0 && at(s, i - 1) != '\n') i--;
    return i;
}

// The first newline in `s` at or after `from`, or the end.
static size_t builtin_line_end(Str s, size_t from) {
    size_t i = from < s.len ? from : s.len;
    if (i == s.len) return i;
    const char *nl = memchr(&at(s, i), '\n', s.len - i);
    return nl != NULL ? (size_t)(nl - s.ptr) : s.len;
}

// The newlines in `s`.
static uint64_t builtin_newlines(Str s) {
    uint64_t n = 0;
    for (size_t i = 0; i < s.len; i++) n += at_unchecked(s, i, "i < s.len") == '\n';
    return n;
}

// `grep(p, s)`: the lines of the file holding the literal `s`, as
// `{line, text}`, each line once. The file is read whole, at most the
// call-bytes cap. Zig's `grepFile`.
static STATUS_MUST_USE Status bi_grep(Run *r, Pos pos, ConstValues args, Value *out) {
    Builtin b = BUILTIN_GREP;
    StorePath p;
    TRY(builtin_path(r, pos, b, &at(args, 0), false, &p));
    Str needle = {0};
    TRY(builtin_string_arg(r, pos, b, &at(args, 1), S("what it looks for"), &needle));
    if (needle.len == 0) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`grep` of \"\" is an error: there is nothing to look for"));
    TRY(builtin_count_read(r, pos));
    uint64_t size = 0;
    TRY(builtin_file_size(r, pos, b, &p, &size));
    uint64_t cap = r->ctx.budgets.call_bytes;
    if (size > cap) {
        return run_failf(r, pos, CLASS_ARGUMENT, "`grep` reads a file whole, at most {d} bytes in one call, and {s} is {d}: `read_lines` and `contains` go through it a part at a time", S("`grep` reads a file whole, at most "),
                         run_unum(r, cap), S(" bytes in one call, and "), p.rel, S(" is "), run_unum(r, size), S(": `read_lines` and `contains` go through it a part at a time"));
    }
    Str whole = {0};
    TRY(builtin_read_sized(r, pos, b, &p, size, 0, false, 0, &whole));
    Finder f;
    finder_init(&f, r, pos, needle);
    Table *list = NULL;
    TRY(builtin_new_list(r, pos, &list));
    int64_t line = 1;
    size_t counted = 0;
    size_t from = 0;
    for (;;) {
        bool found = false;
        size_t i = 0;
        TRY(finder_find(&f, whole, from, &found, &i));
        if (!found) break;
        size_t start = builtin_line_start(whole, i);
        size_t end = builtin_line_end(whole, i);
        line += (int64_t)builtin_newlines(str_sub(whole, counted, start));
        counted = start;
        TRY(run_charge(r, pos, end - start));
        Table *m = NULL;
        TRY(run_new_table(r, pos, &m));
        m->kind = TABLE_MAP;
        TRY(run_map_put_int(r, pos, m, S("line"), line));
        TRY(run_map_put_str(r, pos, m, S("text"), str_sub(whole, start, end)));
        Value mv;
        value_table(&mv, m);
        TRY(run_list_push(r, pos, list, &mv));
        from = end + 1;
        if (from > whole.len) break;
    }
    value_table(out, list);
    return OK;
}

// `line_at(p, offset)`: the whole line holding byte `offset`, without its
// newline, or nil at or past the end; read backward and forward a piece at a
// time, the line at most the call-bytes cap. Zig's `lineAt`.
static STATUS_MUST_USE Status bi_line_at(Run *r, Pos pos, ConstValues args, Value *out) {
    Builtin b = BUILTIN_LINE_AT;
    StorePath p;
    TRY(builtin_path(r, pos, b, &at(args, 0), false, &p));
    uint64_t offset = 0;
    TRY(builtin_offset_arg(r, pos, b, &at(args, 1), &offset));
    TRY(builtin_count_read(r, pos));
    uint64_t size = 0;
    TRY(builtin_file_size(r, pos, b, &p, &size));
    if (offset >= size) {
        value_nil(out);
        return OK;
    }
    uint64_t cap = r->ctx.budgets.call_bytes;
    uint64_t piece_most = builtin_min_u64(BUILTIN_LINE_PIECE, cap);
    uint64_t lo = offset;
    while (lo > 0) {
        uint64_t take = builtin_min_u64(lo, piece_most);
        Str piece = {0};
        TRY(builtin_read_sized(r, pos, b, &p, size, lo - take, true, take, &piece));
        size_t start = builtin_line_start(piece, piece.len);
        if (start > 0) {
            lo = lo - take + start;
            break;
        }
        lo -= take;
        if (offset - lo >= cap) return builtin_line_too_long(r, pos, b, offset, cap);
    }
    uint64_t hi = offset;
    while (hi < size) {
        Str piece = {0};
        TRY(builtin_read_sized(r, pos, b, &p, size, hi, true, builtin_min_u64(piece_most, size - hi), &piece));
        size_t nl = builtin_line_end(piece, 0);
        if (nl < piece.len) {
            hi += nl;
            break;
        }
        hi += piece.len;
        if (hi - lo > cap) return builtin_line_too_long(r, pos, b, offset, cap);
    }
    if (hi - lo > cap) return builtin_line_too_long(r, pos, b, offset, cap);
    Str got = {0};
    TRY(builtin_read_sized(r, pos, b, &p, size, lo, true, hi - lo, &got));
    value_string(out, got);
    return OK;
}

// How much of a file a scan holds at once.
enum { BUILTIN_SCAN_CHUNK = 64 * 1024 };

// A scan's range: from `offset`, `n` bytes, or to the end.
typedef struct {
    uint64_t offset;
    bool has_n;
    uint64_t n;
} ScanRange;

// What a scan found: the matches, the first's offset, the file's size, its
// last byte read, and whether the range reached its end.
typedef struct {
    uint64_t count;
    bool has_first;
    uint64_t first;
    uint64_t size;
    uint8_t last;
    bool to_end;
} Scanned;

// The range a scan's last arguments ask, Zig's `rangeArgs`.
static STATUS_MUST_USE Status builtin_range_args(Run *r, Pos pos, Builtin b, ConstValues args, size_t from, ScanRange *out) {
    memset(out, 0, sizeof *out);
    if (args.len <= from) return OK;
    TRY(builtin_offset_arg(r, pos, b, &at(args, from), &out->offset));
    if (args.len > from + 1) {
        TRY(builtin_count_arg(r, pos, b, &at(args, from + 1), &out->n));
        out->has_n = true;
    }
    return OK;
}

// Reads the file BUILTIN_SCAN_CHUNK bytes at a time into one reused buffer,
// counting `needle`'s non-overlapping matches that start in `range`, or
// stopping at the first (`first_only`), across the chunks' edges. Zig's
// `scan`: one store read a call, every byte counted against the read budget
// and charged.
static STATUS_MUST_USE Status builtin_scan(Run *r, Pos pos, Builtin b, const StorePath *p, Str needle, bool first_only, ScanRange range, Scanned *res) {
    TRY(builtin_count_read(r, pos));
    uint64_t file_size = 0;
    TRY(builtin_file_size(r, pos, b, p, &file_size));
    uint64_t start = builtin_min_u64(range.offset, file_size);
    uint64_t end = range.has_n ? start + builtin_min_u64(range.n, file_size - start) : file_size;
    uint64_t cap = r->ctx.budgets.call_bytes;
    if (end - start > cap) {
        Str who = builtin_who(r, b);
        return run_failf(r, pos, CLASS_ARGUMENT, "`{s}` scans at most {d} bytes in one call, and this would be {d}: scan it in parts, `{s}(p, {s}offset, n)`", S("`"), who, S("` scans at most "), run_unum(r, cap),
                         S(" bytes in one call, and this would be "), run_unum(r, end - start), S(": scan it in parts, `"), who, S("(p, "), b == BUILTIN_COUNT_LINES ? S("") : S("s, "), S("offset, n)`"));
    }
    size_t keep_most = needle.len - 1;
    // Reading at most `needle.len - 1` past the range keeps every match found
    // starting in it.
    uint64_t size = builtin_min_u64(file_size, end + keep_most);
    memset(res, 0, sizeof *res);
    res->size = file_size;
    res->to_end = end == file_size;
    if (end == start) return OK;
    Finder f;
    finder_init(&f, r, pos, needle);
    RunStore *rs = builtin_store_of(r);
    // One buffer serves every scan of the run, sized once for the longest
    // needle and a full chunk: a fresh one a call would cost 64 KiB of arena
    // a file.
    if (rs->scan_buf.len == 0) {
        char *room = arena_alloc(r->ctx.values, 2 * BUILTIN_SCAN_CHUNK, 1);
        if (room == NULL) return run_values_oom(r, pos);
        rs->scan_buf.ptr = room;
        rs->scan_buf.len = 2 * BUILTIN_SCAN_CHUNK;
    }
    size_t buf_len = (size_t)builtin_min_u64(size - start, BUILTIN_SCAN_CHUNK) + keep_most;
    Bytes buf = SLICE_SUB(Bytes, rs->scan_buf, 0, buf_len);
    // `buf[0..kept]` holds the last window's tail, from the file's offset
    // `base`.
    size_t kept = 0;
    uint64_t base = start;
    uint64_t at_ = start;
    while (at_ < size) {
        size_t want = (size_t)builtin_min_u64(size - at_, buf.len - kept);
        if (want > run_sat_sub(r->ctx.budgets.read_bytes, r->read_bytes)) return run_budget_stop(&r->ctx, pos, BUDGET_READ_BYTES);
        size_t got = 0;
        Bytes into = SLICE_SUB(Bytes, buf, kept, kept + want);
        StoreErr e = rs->store.read_at(rs->store.ctx, r->ctx.values, p->full, at_, into, &got);
        if (e != STORE_OK) return builtin_store_failed(r, pos, b, p->rel, e);
        r->read_bytes += got;
        TRY(builtin_charge_bytes(r, pos, got));
        // The file shrank under the scan.
        if (got == 0) break;
        at_ += got;
        Str window = {buf.ptr, kept + got};
        res->last = str_byte(window, window.len - 1);
        size_t from = 0;
        for (;;) {
            bool found = false;
            size_t i = 0;
            TRY(finder_find(&f, window, from, &found, &i));
            if (!found) break;
            if (!res->has_first) {
                res->has_first = true;
                res->first = base + i;
            }
            res->count++;
            if (first_only) return OK;
            from = i + needle.len;
        }
        // Keep the tail that could still start a match: the last `keep_most`
        // bytes no match consumed.
        size_t tail = window.len > keep_most ? window.len - keep_most : 0;
        size_t next = from > tail ? from : tail;
        kept = window.len - next;
        if (kept > 0) memmove(buf.ptr, &at(window, next), kept);
        base += next;
    }
    return OK;
}

// `count_in(p, s[, offset[, n]])` and `find_in`: the matches of `s` counted,
// or the first one's offset (nil for none), without holding the file.
static STATUS_MUST_USE Status builtin_scanner(Run *r, Pos pos, Builtin b, ConstValues args, Value *out) {
    StorePath p;
    TRY(builtin_path(r, pos, b, &at(args, 0), false, &p));
    Str needle = {0};
    TRY(builtin_string_arg(r, pos, b, &at(args, 1), S("what it looks for"), &needle));
    Str who = builtin_who(r, b);
    if (needle.len == 0) return run_failf(r, pos, CLASS_ARGUMENT, "`{s}` of \"\" is an error: there is nothing to look for", S("`"), who, S("` of \"\" is an error: there is nothing to look for"));
    if (needle.len > BUILTIN_SCAN_CHUNK) {
        return run_failf(r, pos, CLASS_ARGUMENT, "`{s}`: what it looks for is longer than {d} bytes", S("`"), who, S("`: what it looks for is longer than "), run_unum(r, BUILTIN_SCAN_CHUNK), S(" bytes"));
    }
    ScanRange range;
    TRY(builtin_range_args(r, pos, b, args, 2, &range));
    Scanned got;
    TRY(builtin_scan(r, pos, b, &p, needle, b == BUILTIN_FIND_IN, range, &got));
    if (b == BUILTIN_COUNT_IN) {
        value_int(out, (int64_t)got.count);
    } else if (got.has_first) {
        value_int(out, (int64_t)got.first);
    } else {
        value_nil(out);
    }
    return OK;
}

static STATUS_MUST_USE Status bi_count_in(Run *r, Pos pos, ConstValues args, Value *out) {
    return builtin_scanner(r, pos, BUILTIN_COUNT_IN, args, out);
}

static STATUS_MUST_USE Status bi_find_in(Run *r, Pos pos, ConstValues args, Value *out) {
    return builtin_scanner(r, pos, BUILTIN_FIND_IN, args, out);
}

// `count_lines(p[, offset[, n]])`: the file's lines, as `#lines(read(p))`
// counts them, without holding the file. For a range, its newlines and the
// last line when the range reaches the end, so the parts' counts add up to
// the whole's.
static STATUS_MUST_USE Status bi_count_lines(Run *r, Pos pos, ConstValues args, Value *out) {
    StorePath p;
    TRY(builtin_path(r, pos, BUILTIN_COUNT_LINES, &at(args, 0), false, &p));
    ScanRange range;
    TRY(builtin_range_args(r, pos, BUILTIN_COUNT_LINES, args, 1, &range));
    Scanned got;
    TRY(builtin_scan(r, pos, BUILTIN_COUNT_LINES, &p, S("\n"), false, range, &got));
    bool unended = got.to_end && got.size > 0 && got.last != '\n';
    value_int(out, (int64_t)(got.count + (unended ? 1 : 0)));
    return OK;
}
