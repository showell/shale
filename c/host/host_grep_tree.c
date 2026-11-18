// host_grep_tree.c: the Linux host's `host("grep_tree", needle, o)`, the walk
// and the search behind the Linux prelude's `grep_tree`. The prelude checks
// `needle`, reads the options and stats a lone file; this file walks the
// place, passes by what the search leaves out, searches each file and makes
// the answer, `{hits, files, passed, cut}`, in C rather than in script.
//
// What a script sees is the prelude's as it was: the same hits in the same
// order, the same `passed` and `cut`, the same stops, and the same reads and
// read bytes, since each store call is the one the prelude made (`walk` a
// part at a time, `read(p, 0, 4096)`, `grep`, `read_lines`, `read`), through
// the same confinement, with the same messages. Only the steps differ: no
// interpreter runs a line of it, so a run is charged what the reads bring (a
// step per 16 bytes), a step and the path's bytes an entry, and each hit's
// text, rather than a script's steps. A literal is found with memmem.
//
// A file is read into a scratch arena of its own, given back once it is
// searched, so a tree is held one file at a time: only what a hit keeps is
// copied into the run's values arena.
//
// Include run.h, builtins.c, builtins_text.c, builtins_store.c and
// builtins_walk.c first.

// An entry the walk gave, or the lone file the prelude stat'ed.
typedef struct {
    Str path;
    Str kind;
    int64_t size;
} GrepTreeEntry;

VEC_DEFINE(GrepTreeEntries, GrepTreeEntry)

// A line a file's search found: its number, from 1, and its text.
typedef struct {
    int64_t line;
    Str text;
} GrepTreeLine;

VEC_DEFINE(GrepTreeLines, GrepTreeLine)

// What the prelude's `grep_tree` asks, its options as it read them.
typedef struct {
    Str place;
    Str prefix;
    Str needle;
    // The needle in lower case, for `nocase`.
    Str lowered;
    bool regex;
    bool nocase;
    // `nocase` as given, for `re_lines`' own check of it.
    Value nocase_given;
    // `ext`, `like`, `context` and `most` as given, each judged where the
    // prelude's loop first used it, so a value of the wrong type stops
    // where it stopped, after the reads it made, with its message.
    Value ext;
    Value like;
    Value context;
    Value most;
} GrepTreeAsk;

// The prelude's build folders, whose files are never searched.
static bool grep_tree_build_part(Str part) {
    return str_eq(part, S("zig-out")) || str_eq(part, S("zig-pkg")) || str_eq(part, S("node_modules"));
}

// The newlines in `s`, counted a memchr at a time.
static uint64_t grep_tree_newlines(Str s) {
    uint64_t n = 0;
    Str rest = s;
    for (;;) {
        const char *nl = str_find_byte(rest, '\n');
        if (nl == NULL) return n;
        n++;
        size_t i = (size_t)(nl - rest.ptr);
        rest = str_sub(rest, i + 1, rest.len);
    }
}

// The lines `lines(s)` gives of `s`: a newline ends each, and a last line
// without one is a line too.
static uint64_t grep_tree_line_count(Str s) {
    uint64_t n = grep_tree_newlines(s);
    if (s.len > 0 && str_byte(s, s.len - 1) != '\n') n++;
    return n;
}

// `s` in lower case (ASCII), in `into`, as `lower` makes it.
static STATUS_MUST_USE Status grep_tree_lower(Run *r, Pos pos, Arena *into, Str s, Str *out) {
    char *p = arena_alloc(into, s.len, 1);
    if (p == NULL) return run_values_oom(r, pos);
    Bytes o = {p, s.len};
    for (size_t k = 0; k < s.len; k++) {
        uint8_t c = str_byte(s, k);
        if (c >= 'A' && c <= 'Z') c = (uint8_t)(c + 32);
        at(o, k) = (char)c;
    }
    Str t = {p, s.len};
    *out = t;
    return OK;
}

// The lines of `text` that hold `needle`, each once, numbered from `base` +
// 1, found in `hay` (`text` itself, or `text` in lower case, the same
// length): `grep`'s rule, a line is the one the match starts in. Each line's
// text is `text`'s, and each line found is charged its bytes, as `grep`'s are.
static STATUS_MUST_USE Status grep_tree_find_lines(Run *r, Pos pos, Str text, Str hay, Str needle, int64_t base, GrepTreeLines *found) {
    int64_t line = base + 1;
    size_t counted = 0;
    size_t from = 0;
    while (from < hay.len) {
        Str rest = str_sub(hay, from, hay.len);
        size_t off = mem_find(rest.ptr, rest.len, needle.ptr, needle.len);
        if (off == SIZE_MAX) break;
        size_t i = from + off;
        size_t start = builtin_line_start(hay, i);
        size_t end = builtin_line_end(hay, i);
        line += (int64_t)grep_tree_newlines(str_sub(hay, counted, start));
        counted = start;
        TRY(run_charge(r, pos, end - start));
        GrepTreeLine l = {line, str_sub(text, start, end)};
        if (GrepTreeLines_push(found, l) != OK) return run_values_oom(r, pos);
        from = end + 1;
    }
    return OK;
}

// `read_lines(p, at)` as the prelude called it, into `into`: whole lines from
// `at`, a part of 64 KiB (or the call-bytes cap) and on to the end of the
// line it stops in. `*part` is the bytes, `*next` where the next part starts
// and `*done` whether this one reached the file's end. The reads, read bytes
// and stops are `read_lines`' own.
static STATUS_MUST_USE Status grep_tree_read_part(Run *r, Pos pos, const StorePath *p, uint64_t offset, Arena *into, Str *part, uint64_t *next, bool *done) {
    Builtin b = BUILTIN_READ_LINES;
    uint64_t cap = r->ctx.budgets.call_bytes;
    uint64_t n = builtin_min_u64(BUILTIN_READ_LINES_PART, cap);
    TRY(builtin_count_read(r, pos));
    uint64_t size = 0;
    TRY(builtin_file_size(r, pos, b, p, &size));
    uint64_t start = builtin_min_u64(offset, size);
    Buf got = buf_make(into);
    Str first = {0};
    TRY(builtin_read_sized_in(r, pos, b, p, size, start, n, into, &first));
    if (buf_str(&got, first) != OK) return run_values_oom(r, pos);
    uint64_t end = start + got.len;
    if (end < size && at(got, got.len - 1) != '\n') {
        for (;;) {
            if (end - start >= cap) return builtin_line_too_long(r, pos, b, start, cap);
            Str piece = {0};
            TRY(builtin_read_sized_in(r, pos, b, p, size, end, builtin_min_u64(BUILTIN_LINE_PIECE, cap - (end - start)), into, &piece));
            if (piece.len == 0) break;
            const char *nl = str_find_byte(piece, '\n');
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
    *part = buf_view(&got);
    *next = end;
    *done = end >= size;
    return OK;
}

// One part's hits for a pattern, as the prelude's `re_lines(join(lines,
// "\n"), needle, {nocase})` gave them: each line searched alone, so `^`, `$`
// and `\b` see its edges, numbered from `base` + 1. A last empty line of the
// part is no line of the join, so it is not searched, as it was not.
static STATUS_MUST_USE Status grep_tree_regex_part(Run *r, Pos pos, const GrepTreeAsk *ask, Str part, int64_t base, GrepTreeLines *found) {
    RegexOptions options;
    memset(&options, 0, sizeof options);
    TRY(builtin_flag_option(r, pos, S("re_lines"), S("nocase"), &ask->nocase_given, &options.nocase));
    RegexCached *c = NULL;
    TRY(builtin_program(r, pos, S("re_lines"), ask->needle, options, &c));
    Str joined = part;
    if (joined.len > 0 && str_byte(joined, joined.len - 1) == '\n') joined = str_sub(joined, 0, joined.len - 1);
    TRY(builtin_charge_bytes(r, pos, joined.len));
    Searches each = {0, 0};
    int64_t n = base;
    Str rest = joined;
    while (rest.len > 0) {
        const char *nl = str_find_byte(rest, '\n');
        size_t end = nl == NULL ? rest.len : (size_t)(nl - rest.ptr);
        Str line = str_sub(rest, 0, end);
        rest = nl == NULL ? S("") : str_sub(rest, end + 1, rest.len);
        n++;
        bool hit = false;
        RegexFound match;
        TRY(searches_from(&each, r, pos, c, line, 0, &hit, &match));
        if (!hit) continue;
        TRY(run_charge(r, pos, 1 + line.len));
        GrepTreeLine l = {n, line};
        if (GrepTreeLines_push(found, l) != OK) return run_values_oom(r, pos);
    }
    return searches_done(&each, r, pos);
}

// A file a part at a time, as the prelude's `grep_all` and its loop for a
// pattern or either case read it: each part's lines numbered across the
// parts. A literal is found by memmem in the part (in lower case for
// `nocase`); a needle that holds a newline is in no line.
static STATUS_MUST_USE Status grep_tree_by_parts(Run *r, Pos pos, const GrepTreeAsk *ask, const StorePath *p, Arena *into, GrepTreeLines *found) {
    bool in_a_line = str_find_byte(ask->needle, '\n') == NULL;
    int64_t base = 0;
    uint64_t offset = 0;
    for (;;) {
        Str part = S("");
        uint64_t next = 0;
        bool done = false;
        TRY(grep_tree_read_part(r, pos, p, offset, into, &part, &next, &done));
        if (ask->regex) {
            TRY(grep_tree_regex_part(r, pos, ask, part, base, found));
        } else if (in_a_line && ask->nocase) {
            Str hay = S("");
            TRY(grep_tree_lower(r, pos, into, part, &hay));
            TRY(grep_tree_find_lines(r, pos, part, hay, ask->lowered, base, found));
        } else if (in_a_line) {
            TRY(grep_tree_find_lines(r, pos, part, part, ask->needle, base, found));
        }
        base += (int64_t)grep_tree_line_count(part);
        if (done) return OK;
        offset = next;
    }
}

// `grep(p, needle)` as the prelude called it, into `into`: the file read
// whole, at most the call-bytes cap, with `grep`'s reads, stops and lines.
static STATUS_MUST_USE Status grep_tree_grep(Run *r, Pos pos, const GrepTreeAsk *ask, const StorePath *p, Arena *into, GrepTreeLines *found) {
    Builtin b = BUILTIN_GREP;
    TRY(builtin_count_read(r, pos));
    uint64_t size = 0;
    TRY(builtin_file_size(r, pos, b, p, &size));
    uint64_t cap = r->ctx.budgets.call_bytes;
    if (size > cap) {
        return run_failf(r, pos, CLASS_ARGUMENT, "`grep` reads a file whole, at most {d} bytes in one call, and {s} is {d}: `read_lines` and `contains` go through it a part at a time", S("`grep` reads a file whole, at most "),
                         run_unum(r, cap), S(" bytes in one call, and "), p->rel, S(" is "), run_unum(r, size), S(": `read_lines` and `contains` go through it a part at a time"));
    }
    Str whole = S("");
    TRY(builtin_read_sized_in(r, pos, b, p, size, 0, BUILTIN_TO_END, into, &whole));
    return grep_tree_find_lines(r, pos, whole, whole, ask->needle, 0, found);
}

// `read(p[, offset, n])` as the prelude called it, into `into`: `n` bytes from
// `offset`, or the whole file for BUILTIN_TO_END.
static STATUS_MUST_USE Status grep_tree_read(Run *r, Pos pos, const StorePath *p, uint64_t offset, uint64_t n, Arena *into, Str *out) {
    TRY(builtin_count_read(r, pos));
    uint64_t size = 0;
    TRY(builtin_file_size(r, pos, BUILTIN_READ, p, &size));
    return builtin_read_sized_in(r, pos, BUILTIN_READ, p, size, offset, n, into, out);
}

// `s` copied into the run's values arena, out of a file's scratch arena.
static STATUS_MUST_USE Status grep_tree_keep(Run *r, Pos pos, Str s, Str *out) {
    if (!arena_dup(r->ctx.values, s, out)) return run_values_oom(r, pos);
    return OK;
}

// `{line, text}` pushed onto `list`, its text kept; the list is a list once it
// holds one, `{}` while it holds none, as the prelude's were.
static STATUS_MUST_USE Status grep_tree_push_line(Run *r, Pos pos, Table *list, int64_t line, Str text) {
    Table *m = NULL;
    TRY(run_new_table(r, pos, &m));
    Str kept = S("");
    TRY(grep_tree_keep(r, pos, text, &kept));
    TRY(run_map_put_int(r, pos, m, S("line"), line));
    TRY(run_map_put_str(r, pos, m, S("text"), kept));
    m->kind = TABLE_MAP;
    list->kind = TABLE_LIST;
    Value v;
    value_table(&v, m);
    return run_list_push(r, pos, list, &v);
}

// What a search of one file adds to the answer.
typedef struct {
    Table *hits;
    Table *passed;
    int64_t files;
    bool cut;
} GrepTreeAnswer;

static STATUS_MUST_USE Status grep_tree_pass(Run *r, Pos pos, GrepTreeAnswer *ans, Str path, Str why) {
    Table *m = NULL;
    TRY(run_new_table(r, pos, &m));
    TRY(run_map_put_str(r, pos, m, S("path"), path));
    TRY(run_map_put_str(r, pos, m, S("why"), why));
    m->kind = TABLE_MAP;
    ans->passed->kind = TABLE_LIST;
    Value v;
    value_table(&v, m);
    return run_list_push(r, pos, ans->passed, &v);
}

// The `ext` list, or NULL for any extension: what the prelude's `#exts > 0`
// and `for x in exts` took, and their stops for what they refused.
static STATUS_MUST_USE Status grep_tree_exts(Run *r, Pos pos, const Value *v, Table **out) {
    *out = NULL;
    if (v->tag == VALUE_STRING) {
        if (v->as.string.len == 0) return OK;
        return run_fail(&r->ctx, pos, CLASS_TYPE, S("`for x in` takes a list, given a string"));
    }
    if (v->tag != VALUE_TABLE) return run_failf(r, pos, CLASS_TYPE, "`#` takes a string or a list, not {s}", S("`#` takes a string or a list, not "), value_type_name(v));
    if (v->as.table->kind == TABLE_MAP) return run_fail(&r->ctx, pos, CLASS_TYPE, S("`#` of a map: use `count(m)`"));
    if (v->as.table->list.len > 0) *out = v->as.table;
    return OK;
}

// Whether `v` is past 0, by the comparison the prelude made: an integer, or
// `>`'s own stop.
static STATUS_MUST_USE Status grep_tree_positive(Run *r, Pos pos, const Value *v, bool *out) {
    Value zero;
    value_int(&zero, 0);
    Value got;
    TRY(run_compare(r, pos, BINOP_GT, v, &zero, &got));
    *out = got.as.boolean;
    return OK;
}

// Whether the entry is a file of the search at all, as the prelude's
// `grep_tree_skip` judged it before reading: not a folder, nothing under a
// build's folder, an extension and a name asked for. `*named` says whether
// every part of `rest` is a name the store can hold.
static STATUS_MUST_USE Status grep_tree_wanted(Run *r, Pos pos, const GrepTreeAsk *ask, const GrepTreeEntry *e, Str rest, bool *wanted, bool *named) {
    *wanted = false;
    *named = true;
    if (str_eq(e->kind, S("directory"))) return OK;
    size_t from = 0;
    for (;;) {
        Str tail = str_sub(rest, from, rest.len);
        const char *slash = str_find_byte(tail, '/');
        size_t to = slash == NULL ? rest.len : from + (size_t)(slash - tail.ptr);
        Str part = str_sub(rest, from, to);
        if (grep_tree_build_part(part)) return OK;
        if (!cli_fat_name(part)) *named = false;
        if (slash == NULL) break;
        from = to + 1;
    }
    Table *exts = NULL;
    TRY(grep_tree_exts(r, pos, &ask->ext, &exts));
    if (exts != NULL) {
        bool ext = false;
        for (size_t k = 0; k < exts->list.len; k++) {
            const Value *x = &at(exts->list, k);
            IntText it;
            Str name = S("");
            if (x->tag == VALUE_STRING) {
                name = x->as.string;
            } else if (x->tag == VALUE_INT) {
                name = int_text(&it, x->as.integer);
            } else {
                return run_failf(r, pos, CLASS_TYPE, "`grep_tree`'s `ext` holds {s}: an extension is a string", S("`grep_tree`'s `ext` holds "), value_type_name(x), S(": an extension is a string"));
            }
            bool ends = e->path.len > name.len && str_byte(e->path, e->path.len - name.len - 1) == '.' && str_eq(str_sub(e->path, e->path.len - name.len, e->path.len), name);
            if (ends) ext = true;
        }
        if (!ext) return OK;
    }
    if (!(ask->like.tag == VALUE_STRING && ask->like.as.string.len == 0)) {
        Str pattern = S("");
        TRY(builtin_string_arg(r, pos, BUILTIN_LIKE, &ask->like, S("its pattern"), &pattern));
        size_t k = e->path.len;
        while (k > 0 && str_byte(e->path, k - 1) != '/') k--;
        bool like = false;
        TRY(builtin_like(r, pos, str_sub(e->path, k, e->path.len), pattern, &like));
        if (!like) return OK;
    }
    *wanted = true;
    return OK;
}

// The hits of one file found, pushed onto the answer up to `most`, each with
// its `before` and `after` lines when `context` asks: the file read whole
// again for them, as the prelude's `lines(read(p))` did.
static STATUS_MUST_USE Status grep_tree_hits(Run *r, Pos pos, const GrepTreeAsk *ask, const GrepTreeEntry *e, const StorePath *p, Arena *into, GrepTreeLines found, GrepTreeAnswer *ans) {
    GrepTreeLines all = GrepTreeLines_make(into);
    bool context = false;
    TRY(grep_tree_positive(r, pos, &ask->context, &context));
    if (context && found.len > 0) {
        Str whole = S("");
        TRY(grep_tree_read(r, pos, p, 0, BUILTIN_TO_END, into, &whole));
        TRY(run_charge(r, pos, whole.len));
        Str rest = whole;
        int64_t n = 0;
        while (rest.len > 0) {
            const char *nl = str_find_byte(rest, '\n');
            size_t end = nl == NULL ? rest.len : (size_t)(nl - rest.ptr);
            n++;
            GrepTreeLine l = {n, str_sub(rest, 0, end)};
            if (GrepTreeLines_push(&all, l) != OK) return run_values_oom(r, pos);
            rest = nl == NULL ? S("") : str_sub(rest, end + 1, rest.len);
        }
    }
    for (size_t k = 0; k < found.len; k++) {
        Value held;
        value_int(&held, (int64_t)ans->hits->list.len);
        Value full;
        TRY(run_compare(r, pos, BINOP_GE, &held, &ask->most, &full));
        if (full.as.boolean) {
            ans->cut = true;
            break;
        }
        GrepTreeLine m = at(found, k);
        Table *hit = NULL;
        TRY(run_new_table(r, pos, &hit));
        Str text = S("");
        TRY(grep_tree_keep(r, pos, m.text, &text));
        TRY(run_map_put_str(r, pos, hit, S("path"), e->path));
        TRY(run_map_put_int(r, pos, hit, S("line"), m.line));
        TRY(run_map_put_str(r, pos, hit, S("text"), text));
        if (context) {
            int64_t around = ask->context.as.integer;
            Table *before = NULL;
            Table *after = NULL;
            TRY(run_new_table(r, pos, &before));
            TRY(run_new_table(r, pos, &after));
            for (int64_t i = m.line - around; i <= m.line - 1; i++) {
                if (i < 1) continue;
                if ((uint64_t)i > all.len) return run_failf(r, pos, CLASS_STORE, "`grep_tree`: {s} changed while it was searched", S("`grep_tree`: "), e->path, S(" changed while it was searched"));
                TRY(grep_tree_push_line(r, pos, before, i, at(all, (size_t)i - 1).text));
            }
            for (int64_t i = m.line + 1; i <= m.line + around; i++) {
                if ((uint64_t)i <= all.len) TRY(grep_tree_push_line(r, pos, after, i, at(all, (size_t)i - 1).text));
            }
            Value bv;
            Value av;
            value_table(&bv, before);
            value_table(&av, after);
            TRY(run_map_put(r, pos, hit, S("before"), &bv));
            TRY(run_map_put(r, pos, hit, S("after"), &av));
        }
        hit->kind = TABLE_MAP;
        ans->hits->kind = TABLE_LIST;
        Value hv;
        value_table(&hv, hit);
        TRY(run_list_push(r, pos, ans->hits, &hv));
    }
    return OK;
}

// One entry judged, and searched when it is a file of the search, its bytes
// in `into`. The first store call confines its path, by the builtin the
// prelude called first (`read` for the binary check, else `grep` or
// `read_lines`), so a refusal (a link, a secrets mount's folder) says what it
// said; the path stays confined for the calls after it.
static STATUS_MUST_USE Status grep_tree_entry(Run *r, Pos pos, const GrepTreeAsk *ask, const GrepTreeEntry *e, Arena *into, GrepTreeAnswer *ans) {
    Str rest = e->path;
    if (ask->prefix.len > 0 && str_starts_with(e->path, ask->prefix)) rest = str_sub(e->path, ask->prefix.len, e->path.len);
    if (ask->prefix.len == 0 && str_eq(e->path, ask->place)) {
        size_t k = ask->place.len;
        while (k > 0 && str_byte(ask->place, k - 1) != '/') k--;
        rest = str_sub(ask->place, k, ask->place.len);
    }
    TRY(run_charge(r, pos, 1));
    TRY(builtin_charge_bytes(r, pos, e->path.len));
    bool wanted = false;
    bool named = true;
    TRY(grep_tree_wanted(r, pos, ask, e, rest, &wanted, &named));
    if (!wanted) return OK;
    if (!str_eq(e->kind, S("file"))) return grep_tree_pass(r, pos, ans, e->path, S("a symbolic link, never followed"));
    if (!named) return grep_tree_pass(r, pos, ans, e->path, S("a name the store can't hold"));
    bool lines = ask->regex || ask->nocase || e->size > 1048576;
    Builtin first = e->size > 0 ? BUILTIN_READ : lines ? BUILTIN_READ_LINES : BUILTIN_GREP;
    Value pv;
    value_string(&pv, e->path);
    StorePath p;
    TRY(builtin_path(r, pos, first, &pv, false, &p));
    if (e->size > 0) {
        Str head = S("");
        TRY(grep_tree_read(r, pos, &p, 0, 4096, into, &head));
        if (str_find_byte(head, '\0') != NULL) return grep_tree_pass(r, pos, ans, e->path, S("binary"));
    }
    ans->files++;
    GrepTreeLines found = GrepTreeLines_make(into);
    if (lines) {
        TRY(grep_tree_by_parts(r, pos, ask, &p, into, &found));
    } else {
        TRY(grep_tree_grep(r, pos, ask, &p, into, &found));
    }
    return grep_tree_hits(r, pos, ask, e, &p, into, found, ans);
}

// An entry of a list of `{path, kind, size}` maps, the walk's or the lone
// file's.
static STATUS_MUST_USE Status grep_tree_entry_of(Run *r, Pos pos, const Value *v, GrepTreeEntry *out) {
    Value path;
    Value kind;
    Value size;
    if (v->tag != VALUE_TABLE || v->as.table->kind != TABLE_MAP || !ValueMap_get(&v->as.table->map, S("path"), &path) || !ValueMap_get(&v->as.table->map, S("kind"), &kind) ||
        !ValueMap_get(&v->as.table->map, S("size"), &size) || path.tag != VALUE_STRING || kind.tag != VALUE_STRING || size.tag != VALUE_INT) {
        return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`grep_tree`'s `entries` are each a map of a path, a kind and a size"));
    }
    out->path = path.as.string;
    out->kind = kind.as.string;
    out->size = size.as.integer;
    return OK;
}

// Every entry under `place` but those `.*` passes by, as the prelude's
// `walk_all(place, {skip = ".*"})` gave them: `walk` called a part at a time,
// each part resumed after the last one's `more`, so its reads are the same.
static STATUS_MUST_USE Status grep_tree_walk(Run *r, Pos pos, Str place, GrepTreeEntries *out) {
    Str after = S("");
    for (;;) {
        Table *o = NULL;
        TRY(run_new_table(r, pos, &o));
        TRY(run_map_put_str(r, pos, o, S("after"), after));
        TRY(run_map_put_str(r, pos, o, S("skip"), S(".*")));
        o->kind = TABLE_MAP;
        Value argv[2];
        Values av = {argv, 2};
        value_string(&at(av, 0), place);
        value_table(&at(av, 1), o);
        ConstValues args = {argv, 2};
        Value part;
        TRY(bi_walk(r, pos, args, &part));
        bool more = false;
        Table *list = part.as.table;
        for (size_t k = 0; k < list->list.len; k++) {
            GrepTreeEntry e;
            TRY(grep_tree_entry_of(r, pos, &at(list->list, k), &e));
            TRY(run_charge(r, pos, 1));
            if (str_eq(e.kind, S("more"))) {
                more = true;
                after = e.path;
                continue;
            }
            if (GrepTreeEntries_push(out, e) != OK) return run_values_oom(r, pos);
        }
        if (!more) return OK;
    }
}

// The option `name` of the prelude's map, which it always gives.
static STATUS_MUST_USE Status grep_tree_option(Run *r, Pos pos, Table *o, Str name, Value *out) {
    if (ValueMap_get(&o->map, name, out)) return OK;
    return run_failf(r, pos, CLASS_ARGUMENT, "`grep_tree` is given no `{s}`", S("`grep_tree` is given no `"), name, S("`"));
}

static STATUS_MUST_USE Status grep_tree_str_option(Run *r, Pos pos, Table *o, Str name, Str *out) {
    Value v;
    TRY(grep_tree_option(r, pos, o, name, &v));
    if (v.tag != VALUE_STRING) return run_failf(r, pos, CLASS_TYPE, "`grep_tree`'s `{s}` must be a string, not {s}", S("`grep_tree`'s `"), name, S("` must be a string, not "), value_type_name(&v));
    *out = v.as.string;
    return OK;
}

// The prelude's options read into `ask`: `place`, `prefix`, `regex`,
// `nocase`, `ext`, `like`, `context` and `most`.
static STATUS_MUST_USE Status grep_tree_ask(Run *r, Pos pos, Str needle, Table *o, GrepTreeAsk *ask) {
    memset(ask, 0, sizeof *ask);
    ask->needle = needle;
    TRY(grep_tree_str_option(r, pos, o, S("place"), &ask->place));
    TRY(grep_tree_str_option(r, pos, o, S("prefix"), &ask->prefix));
    Value v;
    TRY(grep_tree_option(r, pos, o, S("regex"), &v));
    ask->regex = value_truthy(&v);
    TRY(grep_tree_option(r, pos, o, S("nocase"), &ask->nocase_given));
    ask->nocase = value_truthy(&ask->nocase_given);
    TRY(grep_tree_option(r, pos, o, S("ext"), &ask->ext));
    TRY(grep_tree_option(r, pos, o, S("like"), &ask->like));
    TRY(grep_tree_option(r, pos, o, S("context"), &ask->context));
    TRY(grep_tree_option(r, pos, o, S("most"), &ask->most));
    return grep_tree_lower(r, pos, r->ctx.values, needle, &ask->lowered);
}

// `host("grep_tree", needle, {place, prefix, entries, regex, nocase, ext,
// like, context, most})`: the Linux prelude's `grep_tree` after its checks.
// `entries` is the lone file the prelude stat'ed, or `{}` to walk `place`.
static STATUS_MUST_USE Status host_grep_tree(Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len != 2) return run_failf(r, pos, CLASS_ARGUMENT, "`grep_tree` takes a needle and the prelude's options, given {d} arguments", S("`grep_tree` takes a needle and the prelude's options, given "), run_unum(r, args.len), S(" arguments"));
    Str needle = S("");
    TRY(builtin_string_arg(r, pos, BUILTIN_HOST, &at(args, 0), S("its needle"), &needle));
    if (needle.len == 0) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`grep_tree` of \"\": every line holds it"));
    Table *o = NULL;
    TRY(builtin_map_arg(r, pos, BUILTIN_HOST, &at(args, 1), &o));
    GrepTreeAsk ask;
    TRY(grep_tree_ask(r, pos, needle, o, &ask));
    Value given;
    TRY(grep_tree_option(r, pos, o, S("entries"), &given));
    if (given.tag != VALUE_TABLE || given.as.table->kind == TABLE_MAP) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`grep_tree`'s `entries` is a list"));
    GrepTreeEntries entries = GrepTreeEntries_make(r->ctx.values);
    if (given.as.table->list.len == 0) {
        TRY(grep_tree_walk(r, pos, ask.place, &entries));
    } else {
        for (size_t k = 0; k < given.as.table->list.len; k++) {
            GrepTreeEntry e;
            TRY(grep_tree_entry_of(r, pos, &at(given.as.table->list, k), &e));
            if (GrepTreeEntries_push(&entries, e) != OK) return run_values_oom(r, pos);
        }
    }
    GrepTreeAnswer ans;
    memset(&ans, 0, sizeof ans);
    TRY(run_new_table(r, pos, &ans.hits));
    TRY(run_new_table(r, pos, &ans.passed));
    for (size_t k = 0; k < entries.len && !ans.cut; k++) {
        // DRAGON: the arena budget. A file's bytes in a scratch arena are
        // bytes the run holds, which the values arena's limit does not see.
        // REMEDY: the scratch arena's limit is what the values arena has
        // left, and it refuses with the run's refusal, so a file too big for
        // the run is the arena budget's stop, as it was in the values arena.
        Arena into;
        arena_init(&into, run_sat_sub(r->ctx.values->limit, r->ctx.values->held), &r->ctx.refused);
        Status s = grep_tree_entry(r, pos, &ask, &at(entries, k), &into, &ans);
        arena_deinit(&into);
        TRY(s);
    }
    Table *m = NULL;
    TRY(run_new_table(r, pos, &m));
    Value hv;
    Value pv;
    value_table(&hv, ans.hits);
    value_table(&pv, ans.passed);
    TRY(run_map_put(r, pos, m, S("hits"), &hv));
    TRY(run_map_put_int(r, pos, m, S("files"), ans.files));
    TRY(run_map_put(r, pos, m, S("passed"), &pv));
    TRY(run_map_put_bool(r, pos, m, S("cut"), ans.cut));
    m->kind = TABLE_MAP;
    value_table(out, m);
    return OK;
}
