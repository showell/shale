// builtins_walk.c: the store's `walk`: every entry under a folder, depth first,
// each folder's names sorted and a folder before what it holds, a part at a
// time, each part resumed after the path the last one ended at. The walk keeps
// its own stack of folders, so a tree's depth is the store's to choose, never
// the C stack's.
//
// A program includes it after builtins_store.c.

// A folder the walk is in: its entries, and the index of the next to take.
typedef struct {
    Str rel;
    Str full;
    StoreEntries entries;
    size_t at;
    uint64_t depth;
} WalkLevel;

VEC_DEFINE(WalkLevels, WalkLevel)

// What `walk`'s second argument asks: how deep, which entries (`like`, `kind`),
// what to pass over (`skip`), where an earlier part ended (`after`), and the
// reads to leave in the budget (`keep_reads`).
typedef struct {
    // WALK_ANY_DEPTH when not given.
    uint64_t depth;
    bool has_like;
    Str like;
    bool has_kind;
    StoreKind kind;
    bool has_after;
    Str after;
    bool has_skip;
    Str skip;
    bool has_keep_reads;
    uint64_t keep_reads;
} WalkOptions;

// A `depth` that holds no walk back: a depth given is at most INT64_MAX.
#define WALK_ANY_DEPTH UINT64_MAX

// The most reads one entry of a walk takes: its stat, and its listing if it
// is a folder walked into.
enum { WALK_ENTRY_READS = 2 };

// `name` under the folder `folder`, as every call takes it: a mount's root
// (`m:`) takes the name with no `/`, the root none at all.
static STATUS_MUST_USE Status walk_under(Run *r, Pos pos, Str folder, Str name, Str *out) {
    if (folder.len == 0) {
        *out = name;
        return OK;
    }
    Buf b = buf_make(r->ctx.values);
    bool colon = str_byte(folder, folder.len - 1) == ':';
    if (buf_str(&b, folder) != OK || (!colon && buf_byte(&b, '/') != OK) || buf_str(&b, name) != OK) return run_values_oom(r, pos);
    *out = buf_view(&b);
    return OK;
}

// An entry's OS path: its folder's, `/`, its name.
static STATUS_MUST_USE Status walk_full(Run *r, Pos pos, Str folder, Str name, Str *out) {
    Buf b = buf_make(r->ctx.values);
    if (buf_str(&b, folder) != OK || buf_byte(&b, '/') != OK || buf_str(&b, name) != OK) return run_values_oom(r, pos);
    *out = buf_view(&b);
    return OK;
}

// Whether `full`, an entry's OS path, is a secrets mount's folder or inside
// it: the walk leaves such an entry out, unlisted, unstat'ed and unnamed, as
// though it were not there. False on metal (PATH_HOOK_NONE).
static STATUS_MUST_USE Status walk_in_secrets(Run *r, Pos pos, Str full, bool *in) {
    *in = false;
    RunStore *rs = builtin_store_of(r);
    const PathHook *hook = rs->path.hook;
    return hook->in_secrets(hook->ctx, &rs->path, pos, full, in);
}

// Whether the folder entry `en`, under `top`, is one the walk goes into
// when `after` resumes through it: not one `skip` passes by, which the walk
// never entered, and not a secrets mount's folder.
static STATUS_MUST_USE Status walk_resumes_into(Run *r, Pos pos, const WalkOptions *o, const WalkLevel *top, StoreEntry en, bool *into) {
    *into = false;
    if (en.kind != STORE_KIND_DIRECTORY || top->depth >= o->depth) return OK;
    if (o->has_skip) {
        bool skipped = false;
        TRY(builtin_like(r, pos, en.name, o->skip, &skipped));
        if (skipped) return OK;
    }
    Str full = S("");
    TRY(walk_full(r, pos, top->full, en.name, &full));
    bool secret = false;
    TRY(walk_in_secrets(r, pos, full, &secret));
    *into = !secret;
    return OK;
}

static uint64_t walk_reads_left(const Run *r) {
    return run_sat_sub(r->ctx.budgets.reads, r->reads);
}

// `a + b`, at most UINT64_MAX.
static uint64_t walk_sat_add(uint64_t a, uint64_t b) {
    return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}

static STATUS_MUST_USE Status walk_options(Run *r, Pos pos, const Value *v, WalkOptions *o) {
    memset(o, 0, sizeof *o);
    o->depth = WALK_ANY_DEPTH;
    Table *m = NULL;
    TRY(builtin_map_arg(r, pos, BUILTIN_WALK, v, &m));
    for (size_t i = 0; i < ValueMap_len(&m->map); i++) {
        Str k = ValueMap_key_at(&m->map, i);
        Value x = ValueMap_value_at(&m->map, i);
        if (str_eq(k, S("depth"))) {
            int64_t d = 0;
            TRY(builtin_int_arg(r, pos, BUILTIN_WALK, &x, S("its depth"), &d));
            if (d < 1) return run_failf(r, pos, CLASS_ARGUMENT, "`walk`: a depth of {d}: 1 is the folder's own entries", S("`walk`: a depth of "), run_num(r, d), S(": 1 is the folder's own entries"));
            o->depth = (uint64_t)d;
        } else if (str_eq(k, S("like"))) {
            TRY(builtin_string_arg(r, pos, BUILTIN_WALK, &x, S("its `like`"), &o->like));
            o->has_like = true;
        } else if (str_eq(k, S("after"))) {
            TRY(builtin_string_arg(r, pos, BUILTIN_WALK, &x, S("its `after`"), &o->after));
            o->has_after = true;
        } else if (str_eq(k, S("skip"))) {
            TRY(builtin_string_arg(r, pos, BUILTIN_WALK, &x, S("its `skip`"), &o->skip));
            o->has_skip = true;
        } else if (str_eq(k, S("keep_reads"))) {
            int64_t n = 0;
            TRY(builtin_int_arg(r, pos, BUILTIN_WALK, &x, S("its `keep_reads`"), &n));
            if (n < 0) return run_failf(r, pos, CLASS_ARGUMENT, "`walk`: `keep_reads` of {d}: it is a count of reads", S("`walk`: `keep_reads` of "), run_num(r, n), S(": it is a count of reads"));
            o->has_keep_reads = true;
            o->keep_reads = (uint64_t)n;
        } else if (str_eq(k, S("kind"))) {
            Str s = {0};
            TRY(builtin_string_arg(r, pos, BUILTIN_WALK, &x, S("its `kind`"), &s));
            if (str_eq(s, S("file"))) {
                o->kind = STORE_KIND_FILE;
            } else if (str_eq(s, S("directory"))) {
                o->kind = STORE_KIND_DIRECTORY;
            } else if (str_eq(s, S("other"))) {
                o->kind = STORE_KIND_OTHER;
            } else {
                return run_failf(r, pos, CLASS_ARGUMENT, "`walk`: a kind of \"{s}\": \"file\", \"directory\" or \"other\"", S("`walk`: a kind of \""), s, S("\": \"file\", \"directory\" or \"other\""));
            }
            o->has_kind = true;
        } else {
            return run_failf(r, pos, CLASS_ARGUMENT, "`walk` takes `depth`, `like`, `kind`, `skip`, `after` and `keep_reads`, not `{s}`", S("`walk` takes `depth`, `like`, `kind`, `skip`, `after` and `keep_reads`, not `"), k, S("`"));
        }
    }
    return OK;
}

// The last entry of a part when there is more: `{path, kind = "more"}`, its
// path the place the next part resumes after.
static STATUS_MUST_USE Status walk_more(Run *r, Pos pos, Str path, Value *out) {
    Table *m = NULL;
    TRY(run_new_table(r, pos, &m));
    TRY(run_map_put_str(r, pos, m, S("path"), path));
    TRY(run_map_put_str(r, pos, m, S("kind"), S("more")));
    TRY(run_map_put_int(r, pos, m, S("size"), 0));
    TRY(run_map_put_int(r, pos, m, S("modified"), 0));
    m->kind = TABLE_MAP;
    value_table(out, m);
    return OK;
}

// Puts the walk back where `after` left it: each folder on `after`'s path
// listed again and set at the first name past `after`'s part, so a walk in
// parts takes each entry once whatever came or went in between. "" or the
// walk's own path is the start.
static STATUS_MUST_USE Status walk_resume(Run *r, Pos pos, const StorePath *p, const WalkOptions *o, WalkLevels *levels) {
    Str after = o->after;
    Str rel = p->rel;
    if (after.len == 0 || str_eq(after, rel)) return OK;
    size_t skip = rel.len + (rel.len > 0 && str_byte(rel, rel.len - 1) != ':' ? 1 : 0);
    Str rest = after;
    if (rel.len > 0) {
        bool under = after.len > skip && str_starts_with(after, rel) && (skip == rel.len || str_byte(after, rel.len) == '/');
        if (!under) {
            return run_failf(r, pos, CLASS_ARGUMENT, "`walk`: `after` is \"{s}\", which is not under \"{s}\": it is a path an earlier walk of it returned", S("`walk`: `after` is \""), after, S("\", which is not under \""), rel,
                             S("\": it is a path an earlier walk of it returned"));
        }
        rest = str_sub(after, skip, after.len);
    }
    size_t from = 0;
    while (from <= rest.len) {
        size_t to = path_part_end(rest, from);
        Str part = str_sub(rest, from, to);
        WalkLevel *top = &at(*levels, levels->len - 1);
        size_t i = 0;
        while (i < top->entries.len && builtin_bytes_less(at(top->entries, i).name, part)) i++;
        TRY(run_charge(r, pos, i + 1));
        if (i == top->entries.len || !str_eq(at(top->entries, i).name, part)) {
            top->at = i;
            return OK;
        }
        top->at = i + 1;
        StoreEntry en = at(top->entries, i);
        bool into = false;
        TRY(walk_resumes_into(r, pos, o, top, en, &into));
        if (!into) return OK;
        WalkLevel next;
        memset(&next, 0, sizeof next);
        TRY(walk_under(r, pos, top->rel, en.name, &next.rel));
        TRY(walk_full(r, pos, top->full, en.name, &next.full));
        next.depth = top->depth + 1;
        TRY(builtin_count_read(r, pos));
        TRY(builtin_sorted_entries(r, pos, BUILTIN_WALK, next.full, next.rel, &next.entries));
        if (WalkLevels_push(levels, next) != OK) return run_values_oom(r, pos);
        if (to == rest.len) break;
        from = to + 1;
    }
    return OK;
}

// One entry kept: `{path, kind, size, modified}`. A link (`other`) is never
// stat'ed: a stat follows links, so a link to `/` would walk the host's files
// and a link loop would recurse; the listing's kind is trusted instead. A
// folder's size is 0, since the hosts disagree on it.
static STATUS_MUST_USE Status walk_entry(Run *r, Pos pos, StoreEntry en, Str rel, Str full, Table *list) {
    bool has_stat = en.kind != STORE_KIND_OTHER;
    StoreStat st;
    memset(&st, 0, sizeof st);
    if (has_stat) {
        TRY(builtin_count_read(r, pos));
        RunStore *rs = builtin_store_of(r);
        StoreErr e = rs->store.stat(rs->store.ctx, r->ctx.values, full, &st);
        if (e != STORE_OK) return builtin_store_failed(r, pos, BUILTIN_WALK, rel, e);
    }
    Table *m = NULL;
    TRY(run_new_table(r, pos, &m));
    TRY(run_map_put_str(r, pos, m, S("path"), rel));
    TRY(run_map_put_str(r, pos, m, S("kind"), builtin_kind_name(en.kind)));
    TRY(run_map_put_int(r, pos, m, S("size"), has_stat && en.kind != STORE_KIND_DIRECTORY ? (int64_t)st.size : 0));
    TRY(run_map_put_int(r, pos, m, S("modified"), has_stat ? st.mtime_s : 0));
    m->kind = TABLE_MAP;
    Value v;
    value_table(&v, m);
    return run_list_push(r, pos, list, &v);
}

// `walk(p[, {depth, like, kind, after, skip, keep_reads}])`: every entry
// under `p` as `{path, size, kind, modified}`, at most `call_entries` a call,
// a `more` entry last when there is more. Each folder listed costs a read and
// each entry kept a stat; an entry filtered out is never stat'ed.
static STATUS_MUST_USE Status bi_walk(Run *r, Pos pos, ConstValues args, Value *out) {
    StorePath p;
    TRY(builtin_path(r, pos, BUILTIN_WALK, &at(args, 0), true, &p));
    WalkOptions o;
    memset(&o, 0, sizeof o);
    o.depth = WALK_ANY_DEPTH;
    if (args.len > 1) TRY(walk_options(r, pos, &at(args, 1), &o));
    Str after = o.has_after ? o.after : S("");
    Table *list = NULL;
    TRY(builtin_new_list(r, pos, &list));
    // When the walk's own stat and listing, the listings to resume at
    // `after` and one entry would eat into `keep_reads`, it reads nothing
    // and gives only the `more` entry, unchanged.
    if (o.has_keep_reads) {
        uint64_t head = 2;
        if (o.has_after) {
            uint64_t slashes = 0;
            for (size_t i = 0; i < after.len; i++) slashes += str_byte(after, i) == '/';
            head += slashes + 1;
        }
        if (walk_reads_left(r) < walk_sat_add(walk_sat_add(o.keep_reads, head), WALK_ENTRY_READS)) {
            Value more;
            TRY(walk_more(r, pos, after, &more));
            TRY(run_list_push(r, pos, list, &more));
            value_table(out, list);
            return OK;
        }
    }
    TRY(builtin_count_read(r, pos));
    bool found = false;
    StoreStat st;
    TRY(builtin_stat_or_null(r, pos, BUILTIN_WALK, &p, &found, &st));
    if (!found) return builtin_store_failed(r, pos, BUILTIN_WALK, p.rel, STORE_FILE_NOT_FOUND);
    if (st.kind != STORE_KIND_DIRECTORY) return builtin_store_failed(r, pos, BUILTIN_WALK, p.rel, STORE_NOT_DIR);
    WalkLevels levels = WalkLevels_make(r->ctx.values);
    WalkLevel first;
    memset(&first, 0, sizeof first);
    first.rel = p.rel;
    first.full = p.full;
    first.depth = 1;
    TRY(builtin_sorted_entries(r, pos, BUILTIN_WALK, p.full, p.rel, &first.entries));
    if (WalkLevels_push(&levels, first) != OK) return run_values_oom(r, pos);
    if (o.has_after) TRY(walk_resume(r, pos, &p, &o, &levels));
    uint64_t considered = 0;
    Str last = S("");
    while (levels.len > 0) {
        WalkLevel *top = &at(levels, levels.len - 1);
        if (top->at == top->entries.len) {
            (void)WalkLevels_pop(&levels);
            continue;
        }
        // An entry in a secrets mount's folder (the folder itself among
        // them) is left out before anything else: never stat'ed, listed,
        // counted or named, not even as where a part ends.
        StoreEntry ahead = at(top->entries, top->at);
        Str full = S("");
        TRY(walk_full(r, pos, top->full, ahead.name, &full));
        bool secret = false;
        TRY(walk_in_secrets(r, pos, full, &secret));
        if (secret) {
            top->at++;
            continue;
        }
        // At most `call_entries` a call, and none past `keep_reads`; with
        // more to come, the list ends with where to resume.
        bool kept_back = o.has_keep_reads && walk_reads_left(r) < walk_sat_add(o.keep_reads, WALK_ENTRY_READS);
        if (considered == r->ctx.budgets.call_entries || kept_back) {
            Value more;
            TRY(walk_more(r, pos, considered == 0 ? after : last, &more));
            TRY(run_list_push(r, pos, list, &more));
            break;
        }
        considered++;
        StoreEntry en = at(top->entries, top->at);
        top->at++;
        Str top_rel = top->rel;
        uint64_t depth = top->depth;
        if (o.has_skip) {
            bool skipped = false;
            TRY(builtin_like(r, pos, en.name, o.skip, &skipped));
            if (skipped) {
                TRY(walk_under(r, pos, top_rel, en.name, &last));
                continue;
            }
        }
        Str rel = S("");
        TRY(walk_under(r, pos, top_rel, en.name, &rel));
        last = rel;
        TRY(run_charge(r, pos, rel.len));
        bool into = en.kind == STORE_KIND_DIRECTORY && depth < o.depth;
        bool keep = !o.has_kind || o.kind == en.kind;
        if (keep && o.has_like) TRY(builtin_like(r, pos, en.name, o.like, &keep));
        if (keep) TRY(walk_entry(r, pos, en, rel, full, list));
        if (into) {
            TRY(builtin_count_read(r, pos));
            WalkLevel next;
            memset(&next, 0, sizeof next);
            next.rel = rel;
            next.full = full;
            next.depth = depth + 1;
            TRY(builtin_sorted_entries(r, pos, BUILTIN_WALK, full, rel, &next.entries));
            if (WalkLevels_push(&levels, next) != OK) return run_values_oom(r, pos);
        }
    }
    value_table(out, list);
    return OK;
}
