// host_grep_tree.c: the Linux host's `host("grep_tree", needle, o)`, the walk
// and the search behind the Linux prelude's `grep_tree`. The prelude checks
// `needle`, reads the options and stats a lone file; this file holds the
// script's options to those it takes, walks the place, passes by what the
// search leaves out, searches each file and makes the answer, `{hits, files,
// passed, cut}`, in C rather than in script.
//
// The walk is one pass, depth first, each folder listed and sorted once, in
// `walk`'s order, so the hits, their order, `passed` and `cut` are what the
// prelude's loop over `walk_all` gave. A read is a folder listed, or a file
// read: its stat and its bytes, its head first, the rest only when the head
// holds no zero byte; a file past the call-bytes cap is read a part a read.
// Each `skip` is a stat. Passed by unlisted: a name that starts with `.`, a
// build's folder, a folder `skip` names, and a secrets mount's folder and
// everything in it, never named. A run is charged a step an entry, each
// listing as `ls` is, a step per 16 bytes read and each hit's text. A
// literal is found with memmem.
//
// A file is read into a scratch arena of its own, given back once it is
// searched, so a tree is held one file at a time: only what a hit keeps is
// copied into the run's values arena.
//
// Include run.h, builtins.c, builtins_text.c, builtins_files.c and
// builtins_walk.c first.

// A file or link the walk met, or the lone file the prelude stat'ed.
typedef struct {
    Str path;
    Str kind;
    int64_t size;
} GrepTreeEntry;

// A line a file's search found: its number, from 1, its text, and which
// needle it holds (an index into the ask's `needles`).
typedef struct {
    int64_t line;
    Str text;
    size_t needle;
} GrepTreeLine;

VEC_DEFINE(GrepTreeLines, GrepTreeLine)

VEC_DEFINE(GrepTreeSkips, Str)

// A needle, and itself in lower case, for `nocase`.
typedef struct {
    Str text;
    Str lowered;
} GrepTreeNeedle;

VEC_DEFINE(GrepTreeNeedles, GrepTreeNeedle)

// What the prelude's `grep_tree` asks, its options as it read them.
typedef struct {
    Str place;
    Str prefix;
    // The needles, one for a string and each of a list's, in its order.
    GrepTreeNeedles needles;
    // A list was given, so each hit names the needle it holds.
    bool many;
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
    // The folders `skip` names, each a path below the place searched.
    GrepTreeSkips skip;
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
static STATUS_MUST_USE Status grep_tree_find_lines(Run *r, Pos pos, Str text, Str hay, Str needle, size_t which, int64_t base, GrepTreeLines *found) {
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
        GrepTreeLine l = {line, str_sub(text, start, end), which};
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
static STATUS_MUST_USE Status grep_tree_read_part(Run *r, Pos pos, const CheckedPath *p, uint64_t offset, Arena *into, Str *part, uint64_t *next, bool *done) {
    Builtin b = BUILTIN_READ_LINES;
    uint64_t cap = r->ctx.budgets.call_bytes;
    uint64_t n = builtin_min_u64(BUILTIN_READ_LINES_PART, cap);
    TRY(builtin_count_read(r, pos));
    uint64_t size = 0;
    TRY(builtin_file_size(r, pos, b, p, &size));
    uint64_t start = builtin_min_u64(offset, size);
    Buf got = buf_make(into);
    uint64_t end = 0;
    TRY(builtin_read_part_in(r, pos, b, p, size, start, n, into, &got, &end));
    *part = buf_view(&got);
    *next = end;
    *done = end >= size;
    return OK;
}

// One part's hits for the pattern `needles[which]`, as the prelude's
// `re_lines(join(lines, "\n"), needle, {nocase})` gave them: each line
// searched alone, so `^`, `$` and `\b` see its edges, numbered from `base` +
// 1. A last empty line of the part is no line of the join, so it is not
// searched, as it was not.
static STATUS_MUST_USE Status grep_tree_regex_part(Run *r, Pos pos, const GrepTreeAsk *ask, size_t which, Str part, int64_t base, GrepTreeLines *found) {
    RegexOptions options;
    memset(&options, 0, sizeof options);
    TRY(builtin_flag_option(r, pos, S("re_lines"), S("nocase"), &ask->nocase_given, &options.nocase));
    RegexCached *c = NULL;
    TRY(builtin_program(r, pos, S("re_lines"), at(ask->needles, which).text, options, &c));
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
        GrepTreeLine l = {n, line, which};
        if (GrepTreeLines_push(found, l) != OK) return run_values_oom(r, pos);
    }
    return searches_done(&each, r, pos);
}

// A file a part at a time, as the prelude's `grep_all` and its loop for a
// pattern or either case read it: each part's lines numbered across the
// parts, and each part searched for every needle, so the file is read once
// however many there are. A literal is found by memmem in the part (in lower
// case for `nocase`, the part lowered once); a needle that holds a newline
// is in no line.
static STATUS_MUST_USE Status grep_tree_by_parts(Run *r, Pos pos, const GrepTreeAsk *ask, const CheckedPath *p, Arena *into, GrepTreeLines *found) {
    int64_t base = 0;
    uint64_t offset = 0;
    for (;;) {
        Str part = S("");
        uint64_t next = 0;
        bool done = false;
        TRY(grep_tree_read_part(r, pos, p, offset, into, &part, &next, &done));
        Str hay = part;
        if (!ask->regex && ask->nocase) TRY(grep_tree_lower(r, pos, into, part, &hay));
        for (size_t k = 0; k < ask->needles.len; k++) {
            const GrepTreeNeedle *nd = &at(ask->needles, k);
            bool in_a_line = str_find_byte(nd->text, '\n') == NULL;
            if (ask->regex) {
                TRY(grep_tree_regex_part(r, pos, ask, k, part, base, found));
            } else if (in_a_line) {
                TRY(grep_tree_find_lines(r, pos, part, hay, ask->nocase ? nd->lowered : nd->text, k, base, found));
            }
        }
        base += (int64_t)grep_tree_line_count(part);
        if (done) return OK;
        offset = next;
    }
}

// One file held whole, searched for `needles[which]` as the prelude
// searched it (`lowered` the file in lower case for `nocase`): a literal by
// `grep`'s rule (a line is the one its match starts in, so a needle may hold
// a newline) in a file of at most 1 MiB, and past it line by line as
// `grep_all`; either case line by line; a pattern each line alone, in the
// parts `read_lines` would have brought (64 KiB, on to a line's end), since
// a part's last empty line was no line of its join and was not searched.
static STATUS_MUST_USE Status grep_tree_search_held_for(Run *r, Pos pos, const GrepTreeAsk *ask, size_t which, Str whole, Str lowered, GrepTreeLines *found) {
    const GrepTreeNeedle *nd = &at(ask->needles, which);
    bool in_a_line = str_find_byte(nd->text, '\n') == NULL;
    if (!ask->regex && !ask->nocase && whole.len <= 1048576) return grep_tree_find_lines(r, pos, whole, whole, nd->text, which, 0, found);
    if (!ask->regex) {
        if (!in_a_line) return OK;
        if (!ask->nocase) return grep_tree_find_lines(r, pos, whole, whole, nd->text, which, 0, found);
        return grep_tree_find_lines(r, pos, whole, lowered, nd->lowered, which, 0, found);
    }
    size_t n = (size_t)builtin_min_u64(BUILTIN_READ_LINES_PART, r->ctx.budgets.call_bytes);
    int64_t base = 0;
    size_t start = 0;
    while (start < whole.len) {
        size_t end = whole.len - start > n ? start + n : whole.len;
        if (end < whole.len && str_byte(whole, end - 1) != '\n') {
            const char *nl = str_find_byte(str_sub(whole, end, whole.len), '\n');
            end = nl == NULL ? whole.len : (size_t)(nl - whole.ptr) + 1;
        }
        Str part = str_sub(whole, start, end);
        TRY(grep_tree_regex_part(r, pos, ask, which, part, base, found));
        base += (int64_t)grep_tree_line_count(part);
        start = end;
    }
    return OK;
}

// The lines many needles found, in line order and, within a line, in the
// needles' order: a stable merge sort, since each needle's lines were found
// in line order, one needle after another. A step an entry a pass.
static STATUS_MUST_USE Status grep_tree_in_line_order(Run *r, Pos pos, Arena *into, GrepTreeLines *found) {
    size_t n = found->len;
    if (n < 2) return OK;
    GrepTreeLine *room = ARENA_ARRAY(into, GrepTreeLine, n);
    if (room == NULL) return run_values_oom(r, pos);
    GrepTreeLinesItems from = {found->ptr, n};
    GrepTreeLinesItems to = {room, n};
    for (size_t width = 1; width < n; width *= 2) {
        TRY(run_charge(r, pos, n));
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            size_t mid = n - lo > width ? lo + width : n;
            size_t hi = n - mid > width ? mid + width : n;
            size_t i = lo;
            size_t j = mid;
            for (size_t k = lo; k < hi; k++) {
                bool left = i < mid && (j >= hi || at(from, i).line <= at(from, j).line);
                if (left) {
                    at(to, k) = at(from, i);
                    i++;
                } else {
                    at(to, k) = at(from, j);
                    j++;
                }
            }
        }
        GrepTreeLinesItems was = from;
        from = to;
        to = was;
    }
    if (from.ptr != found->ptr) mem_copy(found->ptr, from.ptr, n * sizeof *from.ptr);
    return OK;
}

// One file held whole, searched for each needle in turn (lowered once for
// `nocase`), and the lines put in line order when there are many needles.
static STATUS_MUST_USE Status grep_tree_search_held(Run *r, Pos pos, const GrepTreeAsk *ask, Str whole, GrepTreeLines *found) {
    Str lowered = whole;
    if (!ask->regex && ask->nocase) TRY(grep_tree_lower(r, pos, found->arena, whole, &lowered));
    for (size_t k = 0; k < ask->needles.len; k++) TRY(grep_tree_search_held_for(r, pos, ask, k, whole, lowered, found));
    return OK;
}

// `read(p[, offset, n])` as the prelude called it, into `into`: `n` bytes from
// `offset`, or the whole file for BUILTIN_TO_END.
static STATUS_MUST_USE Status grep_tree_read(Run *r, Pos pos, const CheckedPath *p, uint64_t offset, uint64_t n, Arena *into, Str *out) {
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
// every part of `rest` is a name the file system can hold.
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
        if (!path_strict_name(part)) *named = false;
        if (slash == NULL) break; // skip: the last part, the loop's end
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
        TRY(builtin_like_text(r, pos, str_sub(e->path, k, e->path.len), pattern, &like));
        if (!like) return OK;
    }
    *wanted = true;
    return OK;
}

// The hits of one file found, pushed onto the answer up to `most`, each with
// its `before` and `after` lines when `context` asks: from the file held
// whole (`held`), or, for one too big to hold, the file read whole again
// as `read` would, which stops past the call-bytes cap.
static STATUS_MUST_USE Status grep_tree_hits(Run *r, Pos pos, const GrepTreeAsk *ask, Str path, const CheckedPath *p, const Str *held, Arena *into, GrepTreeLines found, GrepTreeAnswer *ans) {
    GrepTreeLines all = GrepTreeLines_make(into);
    bool context = false;
    TRY(grep_tree_positive(r, pos, &ask->context, &context));
    if (context && found.len > 0) {
        Str whole = S("");
        if (held != NULL) {
            whole = *held;
        } else {
            TRY(grep_tree_read(r, pos, p, 0, BUILTIN_TO_END, into, &whole));
        }
        TRY(builtin_charge_bytes(r, pos, whole.len));
        Str rest = whole;
        int64_t n = 0;
        while (rest.len > 0) {
            const char *nl = str_find_byte(rest, '\n');
            size_t end = nl == NULL ? rest.len : (size_t)(nl - rest.ptr);
            n++;
            // A line of the file for `context`, which no needle found.
            GrepTreeLine l = {n, str_sub(rest, 0, end), 0};
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
        TRY(run_map_put_str(r, pos, hit, S("path"), path));
        TRY(run_map_put_int(r, pos, hit, S("line"), m.line));
        TRY(run_map_put_str(r, pos, hit, S("text"), text));
        if (ask->many) TRY(run_map_put_str(r, pos, hit, S("needle"), at(ask->needles, m.needle).text));
        if (context) {
            int64_t around = ask->context.as.integer;
            Table *before = NULL;
            Table *after = NULL;
            TRY(run_new_table(r, pos, &before));
            TRY(run_new_table(r, pos, &after));
            // The lines on each side that the file has: the prelude's loops
            // passed by the rest a step each, so a `context` past the file's
            // length gives what it gave, with no loop over what is not there.
            int64_t first = around >= m.line ? 1 : m.line - around;
            for (int64_t i = first; i <= m.line - 1; i++) {
                if ((uint64_t)i > all.len) return run_failf(r, pos, CLASS_FILE_SYSTEM, "`grep_tree`: {s} changed while it was searched", S("`grep_tree`: "), path, S(" changed while it was searched"));
                TRY(run_charge(r, pos, 1));
                TRY(grep_tree_push_line(r, pos, before, i, at(all, (size_t)i - 1).text));
            }
            int64_t last = (uint64_t)around >= all.len ? (int64_t)all.len : m.line + around;
            if (last > (int64_t)all.len) last = (int64_t)all.len;
            for (int64_t i = m.line + 1; i <= last; i++) {
                TRY(run_charge(r, pos, 1));
                TRY(grep_tree_push_line(r, pos, after, i, at(all, (size_t)i - 1).text));
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

// How much of a file is read first, for the binary test: a zero byte in it
// passes the file by unread past it.
enum { GREP_TREE_HEAD = 4096 };

// One file read and searched: one read of the file system's (its stat and its
// bytes), its head first and the rest only when the head holds no zero byte, so
// a binary file costs its head alone. A file past the call-bytes cap is read a
// part at a time, a read a part, as `read_lines` reads it. Its path is confined
// as every file system call's is, so a link on the way or a secrets mount's
// folder is refused as `read`'s.
static STATUS_MUST_USE Status grep_tree_file(Run *r, Pos pos, const GrepTreeAsk *ask, Str path, Arena *into, GrepTreeAnswer *ans) {
    Value pv;
    value_string(&pv, path);
    CheckedPath p;
    TRY(builtin_checked_path(r, pos, BUILTIN_READ, &pv, false, &p));
    TRY(builtin_count_read(r, pos));
    uint64_t size = 0;
    TRY(builtin_file_size(r, pos, BUILTIN_READ, &p, &size));
    Str head = S("");
    // The head is cut to the call-bytes cap, which a cap under 4 KiB would
    // otherwise refuse for any file past it.
    TRY(builtin_read_sized_in(r, pos, BUILTIN_READ, &p, size, 0, builtin_min_u64(GREP_TREE_HEAD, r->ctx.budgets.call_bytes), into, &head));
    if (str_find_byte(head, '\0') != NULL) return grep_tree_pass(r, pos, ans, path, S("binary")); // nul: a binary file's sniff, no string bound for the kernel
    ans->files++;
    GrepTreeLines found = GrepTreeLines_make(into);
    if (size > r->ctx.budgets.call_bytes) {
        TRY(grep_tree_by_parts(r, pos, ask, &p, into, &found));
        if (ask->needles.len > 1) TRY(grep_tree_in_line_order(r, pos, into, &found));
        return grep_tree_hits(r, pos, ask, path, &p, NULL, into, found, ans);
    }
    Str whole = head;
    if (size > head.len) {
        Str rest = S("");
        TRY(builtin_read_sized_in(r, pos, BUILTIN_READ, &p, size, head.len, BUILTIN_TO_END, into, &rest));
        Buf b = buf_make(into);
        if (buf_str(&b, head) != OK || buf_str(&b, rest) != OK) return run_values_oom(r, pos);
        whole = buf_view(&b);
    }
    TRY(grep_tree_search_held(r, pos, ask, whole, &found));
    if (ask->needles.len > 1) TRY(grep_tree_in_line_order(r, pos, into, &found));
    return grep_tree_hits(r, pos, ask, path, &p, &whole, into, found, ans);
}

// One file or link the walk met (or the lone file the prelude named),
// judged by `grep_tree_wanted` and searched in a scratch arena of its own,
// given back once it is searched.
static STATUS_MUST_USE Status grep_tree_entry(Run *r, Pos pos, const GrepTreeAsk *ask, const GrepTreeEntry *e, Str rest, GrepTreeAnswer *ans) {
    bool wanted = false;
    bool named = true;
    TRY(grep_tree_wanted(r, pos, ask, e, rest, &wanted, &named));
    if (!wanted) return OK;
    if (!str_eq(e->kind, S("file"))) return grep_tree_pass(r, pos, ans, e->path, S("a symbolic link, never followed"));
    if (!named) return grep_tree_pass(r, pos, ans, e->path, S("a name the file system can't hold"));
    Arena into;
    arena_init_within(&into, r->ctx.values);
    Status s = grep_tree_file(r, pos, ask, e->path, &into, ans);
    arena_deinit(&into);
    return s;
}

// An entry's path below the place searched, as the prelude spelled it: past
// the place's `prefix`, or the lone file's own name.
static Str grep_tree_rest(const GrepTreeAsk *ask, Str path) {
    if (ask->prefix.len > 0 && str_starts_with(path, ask->prefix)) return str_sub(path, ask->prefix.len, path.len);
    if (ask->prefix.len == 0 && str_eq(path, ask->place)) {
        size_t k = ask->place.len;
        while (k > 0 && str_byte(ask->place, k - 1) != '/') k--;
        return str_sub(ask->place, k, ask->place.len);
    }
    return path;
}

// Whether the walk goes into the folder at `rest`: not a build's folder (no
// part of `rest` is one), and not one `skip` names.
static bool grep_tree_enters(const GrepTreeAsk *ask, Str rest) {
    size_t from = 0;
    for (;;) {
        Str tail = str_sub(rest, from, rest.len);
        const char *slash = str_find_byte(tail, '/');
        size_t to = slash == NULL ? rest.len : from + (size_t)(slash - tail.ptr);
        if (grep_tree_build_part(str_sub(rest, from, to))) return false;
        if (slash == NULL) break; // skip: the last part, the loop's end
        from = to + 1;
    }
    for (size_t k = 0; k < ask->skip.len; k++) {
        if (str_eq(at(ask->skip, k), rest)) return false;
    }
    return true;
}

// A folder the walk is in: its entries, sorted, and the next to take.
typedef struct {
    Str rel;
    OsStr full;
    FileSystemEntries entries;
    size_t at;
} GrepTreeLevel;

VEC_DEFINE(GrepTreeLevels, GrepTreeLevel)

// The walk, in one pass: depth first, each folder listed and sorted once
// (a read, and a step an entry with the sort's bound, as `ls` charges it),
// a folder before what it holds, each entry a step. Passed by unlisted: a
// name that starts with `.`, a build's folder, a folder `skip` names, and
// a secrets mount's folder and all in it, which is never named. Each file
// is judged and searched as the walk meets it, so a search `most` cuts ends
// the walk there.
static STATUS_MUST_USE Status grep_tree_walk(Run *r, Pos pos, const GrepTreeAsk *ask, GrepTreeAnswer *ans) {
    Value pv;
    value_string(&pv, ask->place);
    CheckedPath p;
    TRY(builtin_checked_path(r, pos, BUILTIN_WALK, &pv, true, &p));
    TRY(builtin_count_read(r, pos));
    bool found = false;
    FileSystemStat st;
    TRY(builtin_stat_or_null(r, pos, BUILTIN_WALK, &p, &found, &st));
    if (!found) return builtin_file_system_failed(r, pos, BUILTIN_WALK, p.rel, FILE_SYSTEM_FILE_NOT_FOUND);
    if (st.kind != FILE_SYSTEM_KIND_DIRECTORY) return builtin_file_system_failed(r, pos, BUILTIN_WALK, p.rel, FILE_SYSTEM_NOT_DIR);
    GrepTreeLevels levels = GrepTreeLevels_make(r->ctx.values);
    GrepTreeLevel first;
    memset(&first, 0, sizeof first);
    first.rel = p.rel;
    first.full = p.full;
    TRY(builtin_sorted_entries(r, pos, BUILTIN_WALK, p.full, p.rel, &first.entries));
    if (GrepTreeLevels_push(&levels, first) != OK) return run_values_oom(r, pos);
    while (levels.len > 0 && !ans->cut) {
        GrepTreeLevel *top = &at(levels, levels.len - 1);
        if (top->at == top->entries.len) {
            (void)GrepTreeLevels_pop(&levels);
            continue;
        }
        FileSystemEntry en = at(top->entries, top->at);
        top->at++;
        TRY(run_charge(r, pos, 1));
        if (str_byte(en.name, 0) == '.') continue;
        Str top_rel = top->rel;
        OsStr full = top->full;
        TRY(walk_full(r, pos, top->full, en.name, &full));
        bool secret = false;
        TRY(walk_in_secrets(r, pos, full, &secret));
        if (secret) continue;
        Str rel = S("");
        TRY(walk_under(r, pos, top_rel, en.name, &rel));
        Str rest = grep_tree_rest(ask, rel);
        if (en.kind == FILE_SYSTEM_KIND_DIRECTORY) {
            if (!grep_tree_enters(ask, rest)) continue;
            TRY(builtin_count_read(r, pos));
            GrepTreeLevel next;
            memset(&next, 0, sizeof next);
            next.rel = rel;
            next.full = full;
            TRY(builtin_sorted_entries(r, pos, BUILTIN_WALK, full, rel, &next.entries));
            if (GrepTreeLevels_push(&levels, next) != OK) return run_values_oom(r, pos);
            continue;
        }
        GrepTreeEntry e = {rel, builtin_kind_name(en.kind), 0};
        TRY(grep_tree_entry(r, pos, ask, &e, rest, ans));
    }
    return OK;
}

// The lone file of a list of one `{path, kind, size}` map, as the prelude
// gives it.
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

// The options a script may give `grep_tree`, in the order its stop lists
// them.
static Names grep_tree_option_names(void) {
    static const Str names[] = {S_ITEM("regex"), S_ITEM("nocase"), S_ITEM("ext"), S_ITEM("like"), S_ITEM("context"), S_ITEM("most"), S_ITEM("skip")};
    Names n = {names, sizeof names / sizeof *names};
    return n;
}

// The script's own `opts` (`given`), each key one grep_tree takes: a key it
// does not take (`max`, a misspelt `nocase`) stops the run, naming the ones
// it does, as `opts_only` does, so no option is passed by quietly.
static STATUS_MUST_USE Status grep_tree_known(Run *r, Pos pos, const Value *given) {
    if (given->tag != VALUE_TABLE || given->as.table->kind != TABLE_MAP) return OK;
    Names names = grep_tree_option_names();
    const ValueMap *m = &given->as.table->map;
    for (size_t k = 0; k < ValueMap_len(m); k++) {
        Str key = ValueMap_key_at(m, k);
        bool known = false;
        for (size_t j = 0; j < names.len; j++) {
            if (str_eq(at(names, j), key)) known = true;
        }
        if (known) continue;
        Buf b = buf_make(r->ctx.values);
        bool ok = buf_str(&b, S("`grep_tree` takes ")) == OK;
        for (size_t j = 0; ok && j < names.len; j++) {
            ok = (j == 0 || buf_str(&b, S(", ")) == OK) && buf_byte(&b, '`') == OK && buf_str(&b, at(names, j)) == OK && buf_byte(&b, '`') == OK;
        }
        ok = ok && buf_str(&b, S(", not `")) == OK && buf_str(&b, key) == OK && buf_byte(&b, '`') == OK;
        if (!ok) return run_values_oom(r, pos);
        return run_fail(&r->ctx, pos, CLASS_FAIL, buf_view(&b));
    }
    return OK;
}

// One needle into `ask`, with itself in lower case; "" is in every line, so
// it stops the run, `name` saying which needle it was.
static STATUS_MUST_USE Status grep_tree_needle(Run *r, Pos pos, Str needle, Str name, GrepTreeAsk *ask) {
    if (needle.len == 0) return run_failf(r, pos, CLASS_FAIL, "`grep_tree` of {s}\"\": every line holds it", S("`grep_tree` of "), name, S("\"\": every line holds it"));
    GrepTreeNeedle nd = {needle, S("")};
    TRY(grep_tree_lower(r, pos, r->ctx.values, needle, &nd.lowered));
    if (GrepTreeNeedles_push(&ask->needles, nd) != OK) return run_values_oom(r, pos);
    return OK;
}

// What `grep_tree` looks for: a string, one needle, or a list of strings,
// each a needle, and then each hit names the needle it holds. A needle of ""
// stops the run, as do an empty list, a map, and a list item or a value
// that is not a string.
static STATUS_MUST_USE Status grep_tree_needles(Run *r, Pos pos, const Value *v, GrepTreeAsk *ask) {
    ask->needles = GrepTreeNeedles_make(r->ctx.values);
    if (v->tag == VALUE_STRING) return grep_tree_needle(r, pos, v->as.string, S(""), ask);
    if (v->tag != VALUE_TABLE) return run_failf(r, pos, CLASS_TYPE, "`grep_tree` looks for a string or a list of strings, not {s}", S("`grep_tree` looks for a string or a list of strings, not "), value_type_name(v));
    const Table *t = v->as.table;
    if (t->kind == TABLE_MAP) return run_fail(&r->ctx, pos, CLASS_TYPE, S("`grep_tree` looks for a string or a list of strings, not a map"));
    if (t->list.len == 0) return run_fail(&r->ctx, pos, CLASS_FAIL, S("`grep_tree` of an empty list: there is nothing to look for"));
    ask->many = true;
    for (size_t k = 0; k < t->list.len; k++) {
        const Value *x = &at(t->list, k);
        IntText it;
        Str nth = int_text(&it, (int64_t)k + 1);
        if (x->tag != VALUE_STRING) return run_failf(r, pos, CLASS_TYPE, "`grep_tree`'s needle {d} is {s}: a needle is a string", S("`grep_tree`'s needle "), nth, S(" is "), value_type_name(x), S(": a needle is a string"));
        Buf b = buf_make(r->ctx.values);
        if (buf_str(&b, S("needle ")) != OK || buf_str(&b, nth) != OK || buf_str(&b, S(", ")) != OK) return run_values_oom(r, pos);
        TRY(grep_tree_needle(r, pos, x->as.string, buf_view(&b), ask));
    }
    return OK;
}

// The prelude's options read into `ask`: `place`, `prefix`, `regex`,
// `nocase`, `ext`, `like`, `context` and `most`, after `given`'s keys are
// held to those a script may give.
static STATUS_MUST_USE Status grep_tree_ask(Run *r, Pos pos, Table *o, GrepTreeAsk *ask) {
    Value given;
    TRY(grep_tree_option(r, pos, o, S("given"), &given));
    TRY(grep_tree_known(r, pos, &given));
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
    return grep_tree_option(r, pos, o, S("most"), &ask->most);
}

// Each folder `skip` names, held to the rules: a path of names below the place
// searched (none absolute, none with a mount's name, an empty part, `.` or
// `..`), and a folder where it is there, each checked by a read of the file
// system's (its stat). One that is not there has nothing to skip and passes,
// as `walk_all`'s `skip` does (a build's `c/out` in a fresh lane). A lone
// file has no folder below it to skip.
static STATUS_MUST_USE Status grep_tree_skips(Run *r, Pos pos, GrepTreeAsk *ask, const Value *given, bool lone) {
    ask->skip = GrepTreeSkips_make(r->ctx.values);
    if (given->tag != VALUE_TABLE || given->as.table->kind != TABLE_MAP) return OK;
    Value v;
    if (!ValueMap_get(&given->as.table->map, S("skip"), &v)) return OK;
    if (v.tag != VALUE_TABLE || v.as.table->kind == TABLE_MAP) {
        return run_failf(r, pos, CLASS_TYPE, "`grep_tree`'s `skip` is a list of folders' paths below the place searched, not {s}", S("`grep_tree`'s `skip` is a list of folders' paths below the place searched, not "), value_type_name(&v));
    }
    for (size_t k = 0; k < v.as.table->list.len; k++) {
        const Value *x = &at(v.as.table->list, k);
        if (x->tag != VALUE_STRING) return run_failf(r, pos, CLASS_TYPE, "`grep_tree`'s `skip` holds {s}: a folder's path is a string", S("`grep_tree`'s `skip` holds "), value_type_name(x), S(": a folder's path is a string"));
        Str dir = x->as.string;
        if (dir.len > 0 && str_byte(dir, 0) == '/') {
            return run_failf(r, pos, CLASS_ARGUMENT, "`grep_tree`'s `skip` \"{s}\" is absolute: a skip is a folder's path below the place searched", S("`grep_tree`'s `skip` \""), dir, S("\" is absolute: a skip is a folder's path below the place searched"));
        }
        bool names = dir.len > 0 && str_find_byte(dir, ':') == NULL;
        size_t from = 0;
        while (names) {
            Str tail = str_sub(dir, from, dir.len);
            const char *slash = str_find_byte(tail, '/');
            size_t to = slash == NULL ? dir.len : from + (size_t)(slash - tail.ptr);
            Str part = str_sub(dir, from, to);
            if (part.len == 0 || str_eq(part, S(".")) || str_eq(part, S(".."))) names = false;
            if (slash == NULL) break; // skip: the last part, the loop's end
            from = to + 1;
        }
        if (!names) {
            return run_failf(r, pos, CLASS_ARGUMENT, "`grep_tree`'s `skip` \"{s}\" is not a path of names: a skip is a folder's path below the place searched, with no mount, empty part, `.` or `..`",
                             S("`grep_tree`'s `skip` \""), dir, S("\" is not a path of names: a skip is a folder's path below the place searched, with no mount, empty part, `.` or `..`"));
        }
        if (lone) {
            return run_failf(r, pos, CLASS_ARGUMENT, "`grep_tree`'s `skip` \"{s}\": the place searched, \"{s}\", is a file, with no folder below it", S("`grep_tree`'s `skip` \""), dir, S("\": the place searched, \""), ask->place, S("\", is a file, with no folder below it"));
        }
        Str under = dir;
        if (ask->prefix.len > 0) {
            Buf b = buf_make(r->ctx.values);
            if (buf_str(&b, ask->prefix) != OK || buf_str(&b, dir) != OK) return run_values_oom(r, pos);
            under = buf_view(&b);
        }
        Value uv;
        value_string(&uv, under);
        CheckedPath sp;
        TRY(builtin_checked_path(r, pos, BUILTIN_HOST, &uv, false, &sp));
        TRY(builtin_count_read(r, pos));
        bool found = false;
        FileSystemStat st;
        TRY(builtin_stat_or_null(r, pos, BUILTIN_HOST, &sp, &found, &st));
        if (!found) continue;
        if (st.kind != FILE_SYSTEM_KIND_DIRECTORY) return run_failf(r, pos, CLASS_ARGUMENT, "`grep_tree`'s `skip` \"{s}\": {s} is not a folder", S("`grep_tree`'s `skip` \""), dir, S("\": "), under, S(" is not a folder"));
        if (GrepTreeSkips_push(&ask->skip, dir) != OK) return run_values_oom(r, pos);
    }
    return OK;
}

// `host("grep_tree", needle, {given, place, prefix, entries, regex, nocase,
// ext, like, context, most})`: the Linux prelude's `grep_tree`, `needle` a
// string or a list of them as the script gave it, `given` the script's own
// options. `entries` is the lone file the prelude stat'ed, or `{}` to walk
// `place`.
static STATUS_MUST_USE Status host_grep_tree(Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len != 2) return run_failf(r, pos, CLASS_ARGUMENT, "`grep_tree` takes a needle and the prelude's options, given {d} arguments", S("`grep_tree` takes a needle and the prelude's options, given "), run_unum(r, args.len), S(" arguments"));
    GrepTreeAsk ask;
    memset(&ask, 0, sizeof ask);
    TRY(grep_tree_needles(r, pos, &at(args, 0), &ask));
    Table *o = NULL;
    TRY(builtin_map_arg(r, pos, BUILTIN_HOST, &at(args, 1), &o));
    TRY(grep_tree_ask(r, pos, o, &ask));
    Value entries;
    TRY(grep_tree_option(r, pos, o, S("entries"), &entries));
    if (entries.tag != VALUE_TABLE || entries.as.table->kind == TABLE_MAP) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`grep_tree`'s `entries` is a list"));
    bool lone = entries.as.table->list.len > 0;
    Value given;
    TRY(grep_tree_option(r, pos, o, S("given"), &given));
    TRY(grep_tree_skips(r, pos, &ask, &given, lone));
    GrepTreeAnswer ans;
    memset(&ans, 0, sizeof ans);
    TRY(run_new_table(r, pos, &ans.hits));
    TRY(run_new_table(r, pos, &ans.passed));
    if (!lone) {
        TRY(grep_tree_walk(r, pos, &ask, &ans));
    } else {
        for (size_t k = 0; k < entries.as.table->list.len && !ans.cut; k++) {
            GrepTreeEntry e;
            TRY(grep_tree_entry_of(r, pos, &at(entries.as.table->list, k), &e));
            TRY(run_charge(r, pos, 1));
            TRY(grep_tree_entry(r, pos, &ask, &e, grep_tree_rest(&ask, e.path), &ans));
        }
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
