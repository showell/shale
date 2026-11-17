// builtins_change.c: the store's builtins that change it: a file written,
// appended to or removed, a folder made or removed, and `edit`, which replaces
// a literal and prints the diff; and `edits_write`, the Linux host's write of
// several files' edits as one. Every change asks the host's hook first, is
// counted against the change budgets, and is logged with its outcome, whatever
// it is.
//
// A program includes it after builtins_store.c.

// One edit: `old` replaced by `new`, every one (`all`) or the one there, and
// only as a whole word (`word`).
typedef struct {
    Str old;
    Str new_text;
    bool all;
    bool word;
} Edit;

SLICE_TYPE(EditList, Edit);

// The most edits one call of the list form takes; each is a pass over the
// file.
enum { CHANGE_EDITS_MOST = 64 };

// A region the edits changed: `was[o0..o1]` became `now[c0..c1]`. Outside
// every span the two texts are the same bytes; the spans are sorted and
// disjoint, so the diff compares the first text with the last however the
// edits overlapped.
typedef struct {
    size_t o0;
    size_t o1;
    size_t c0;
    size_t c1;
} Span;

VEC_DEFINE(Spans, Span)

// Where an edit's `old` is in the text, in order.
VEC_DEFINE(Places, size_t)

// One file's edits worked out and not yet written.
typedef struct {
    StorePath p;
    Str was;
    Str now;
    SpansItems spans;
    size_t n;
} Plan;

typedef enum {
    CHANGE_REPLACE,
    CHANGE_APPEND,
    CHANGE_MKDIR,
    CHANGE_REMOVE,
    CHANGE_REMOVE_TREE,
} ChangeKind;

static bool change_word_byte(uint8_t c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static STATUS_MUST_USE Status change_bool_option(Run *r, Pos pos, Str k, const Value *x, bool *out) {
    if (x->tag == VALUE_BOOL) {
        *out = x->as.boolean;
        return OK;
    }
    return run_failf(r, pos, CLASS_TYPE, "`edit`: its `{s}` must be true or false, not {s}", S("`edit`: its `"), k, S("` must be true or false, not "), value_type_name(x));
}

// `edit`'s arguments: `(p, old, new[, {all, word}])` is one edit,
// `(p, {{old = ..., new = ...[, all][, word]}, ...})` the list.
static STATUS_MUST_USE Status change_edits_arg(Run *r, Pos pos, ConstValues args, EditList *out) {
    Builtin b = BUILTIN_EDIT;
    if (args.len == 2) {
        const Value *l = &at(args, 1);
        if (l->tag != VALUE_TABLE) {
            return run_failf(r, pos, CLASS_TYPE, "`edit` of two arguments takes a list of edits, `{{{{old = ..., new = ...}}, ...}}`, not {s}: or `edit(p, old, new)`",
                             S("`edit` of two arguments takes a list of edits, `{{old = ..., new = ...}, ...}`, not "), value_type_name(l), S(": or `edit(p, old, new)`"));
        }
        Table *t = l->as.table;
        if (t->kind == TABLE_MAP) return run_failf(r, pos, CLASS_TYPE, "`edit`'s second argument is a list of edits, `{{{{old = ..., new = ...}}, ...}}`, not a map", S("`edit`'s second argument is a list of edits, `{{old = ..., new = ...}, ...}`, not a map"));
        if (t->kind == TABLE_EMPTY || t->list.len == 0) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`edit` of no edits: the list is empty"));
        if (t->list.len > CHANGE_EDITS_MOST) {
            return run_failf(r, pos, CLASS_ARGUMENT, "`edit` takes at most {d} edits a call, given {d}: call it again for the rest", S("`edit` takes at most "), run_unum(r, CHANGE_EDITS_MOST), S(" edits a call, given "),
                             run_unum(r, t->list.len), S(": call it again for the rest"));
        }
        Edit *edits = ARENA_ARRAY(r->ctx.values, Edit, t->list.len);
        if (edits == NULL) return run_values_oom(r, pos);
        EditList all = {edits, t->list.len};
        for (size_t k = 0; k < all.len; k++) {
            const Value *v = &at(t->list, k);
            Str nth = run_unum(r, k + 1);
            if (v->tag != VALUE_TABLE || v->as.table->kind != TABLE_MAP) {
                return run_failf(r, pos, CLASS_TYPE, "`edit`: edit {d} must be a map, `{{old = ..., new = ...}}`, not {s}", S("`edit`: edit "), nth, S(" must be a map, `{old = ..., new = ...}`, not "), value_type_name(v));
            }
            const ValueMap *m = &v->as.table->map;
            Edit got = {S(""), S(""), false, false};
            bool has_old = false;
            bool has_new = false;
            for (size_t i = 0; i < ValueMap_len(m); i++) {
                Str key = ValueMap_key_at(m, i);
                Value x = ValueMap_value_at(m, i);
                if (str_eq(key, S("old"))) {
                    TRY(builtin_string_arg(r, pos, b, &x, S("an edit's `old`"), &got.old));
                    has_old = true;
                } else if (str_eq(key, S("new"))) {
                    TRY(builtin_string_arg(r, pos, b, &x, S("an edit's `new`"), &got.new_text));
                    has_new = true;
                } else if (str_eq(key, S("all"))) {
                    TRY(change_bool_option(r, pos, key, &x, &got.all));
                } else if (str_eq(key, S("word"))) {
                    TRY(change_bool_option(r, pos, key, &x, &got.word));
                } else {
                    return run_failf(r, pos, CLASS_ARGUMENT, "`edit`: edit {d} takes `old`, `new`, `all` and `word`, not `{s}`", S("`edit`: edit "), nth, S(" takes `old`, `new`, `all` and `word`, not `"), key, S("`"));
                }
            }
            if (!has_old || !has_new) return run_failf(r, pos, CLASS_ARGUMENT, "`edit`: edit {d} has no `{s}`", S("`edit`: edit "), nth, S(" has no `"), has_old ? S("new") : S("old"), S("`"));
            at(all, k) = got;
        }
        *out = all;
        return OK;
    }
    if (args.len < 3) return run_failf(r, pos, CLASS_ARGUMENT, "`edit` takes `(p, old, new[, opts])` or `(p, {{edits}})`", S("`edit` takes `(p, old, new[, opts])` or `(p, {edits})`"));
    Edit e = {S(""), S(""), false, false};
    TRY(builtin_string_arg(r, pos, b, &at(args, 1), S("what it replaces"), &e.old));
    TRY(builtin_string_arg(r, pos, b, &at(args, 2), S("what it writes"), &e.new_text));
    if (args.len > 3) {
        Table *m = NULL;
        TRY(builtin_map_arg(r, pos, b, &at(args, 3), &m));
        for (size_t i = 0; i < ValueMap_len(&m->map); i++) {
            Str key = ValueMap_key_at(&m->map, i);
            Value x = ValueMap_value_at(&m->map, i);
            bool on = false;
            TRY(change_bool_option(r, pos, key, &x, &on));
            if (str_eq(key, S("all"))) {
                e.all = on;
            } else if (str_eq(key, S("word"))) {
                e.word = on;
            } else {
                return run_failf(r, pos, CLASS_ARGUMENT, "`edit` takes `all` and `word`, not `{s}`", S("`edit` takes `all` and `word`, not `"), key, S("`"));
            }
        }
    }
    Edit *one = ARENA_NEW(r->ctx.values, Edit);
    if (one == NULL) return run_values_oom(r, pos);
    *one = e;
    EditList single = {one, 1};
    *out = single;
    return OK;
}

// A signed difference of two sizes, in 64 bits.
static int64_t change_diff(size_t a, size_t b) {
    return (int64_t)a - (int64_t)b;
}

// Records `now[i..i + len]` replaced by `n` bytes: the spans it meets or
// touches merge into one covering the old text they and the bytes between them
// stood for, and the spans after it shift by the change in length.
static STATUS_MUST_USE Status change_replaced(Run *r, Pos pos, Spans *spans, size_t i, size_t len, size_t n) {
    size_t end = i + len;
    size_t first = 0;
    int64_t shift = 0;
    while (first < spans->len && at(*spans, first).c1 < i) {
        Span s = at(*spans, first);
        shift += change_diff(s.c1, s.c0) - change_diff(s.o1, s.o0);
        first++;
    }
    size_t last = first;
    int64_t inner = 0;
    while (last < spans->len && at(*spans, last).c0 <= end) {
        Span s = at(*spans, last);
        inner += change_diff(s.c1, s.c0) - change_diff(s.o1, s.o0);
        last++;
    }
    size_t met = last - first;
    Span lo = met > 0 ? at(*spans, first) : (Span){0, 0, 0, 0};
    Span hi = met > 0 ? at(*spans, last - 1) : (Span){0, 0, 0, 0};
    size_t c0 = met > 0 && lo.c0 < i ? lo.c0 : i;
    size_t c1 = met > 0 && hi.c1 > end ? hi.c1 : end;
    size_t o0 = met > 0 && lo.c0 <= i ? lo.o0 : (size_t)((int64_t)i - shift);
    size_t o1 = met > 0 && hi.c1 >= end ? hi.o1 : (size_t)((int64_t)end - shift - inner);
    int64_t grown = change_diff(n, len);
    Span merged = {o0, o1, c0, (size_t)((int64_t)c1 + grown)};
    // The met spans become the one merged span: the first takes it and the
    // rest are cut out, or it is put in at `first` when none met.
    if (met == 0) {
        if (Spans_push(spans, merged) != OK) return run_values_oom(r, pos);
        for (size_t k = spans->len - 1; k > first; k--) at(*spans, k) = at(*spans, k - 1);
    } else {
        at(*spans, first) = merged;
        size_t cut = met - 1;
        for (size_t k = first + 1; k + cut < spans->len; k++) at(*spans, k) = at(*spans, k + cut);
        spans->len -= cut;
    }
    at(*spans, first) = merged;
    for (size_t k = first + 1; k < spans->len; k++) {
        Span *s = &at(*spans, k);
        s->c0 = (size_t)((int64_t)s->c0 + grown);
        s->c1 = (size_t)((int64_t)s->c1 + grown);
    }
    return OK;
}

// Where `needle` is in `hay` at or after `from`, or SIZE_MAX.
static size_t change_index_of(Str hay, size_t from, Str needle) {
    if (from > hay.len || needle.len > hay.len - from) return SIZE_MAX;
    if (needle.len == 0) return from;
    const char *hit = memmem(&at(hay, from), hay.len - from, needle.ptr, needle.len);
    return hit != NULL ? (size_t)(hit - hay.ptr) : SIZE_MAX;
}

static Str change_trim(Str s) {
    size_t from = 0;
    size_t to = s.len;
    while (from < to && memchr(" \t\r", str_byte(s, from), 3) != NULL) from++;
    while (to > from && memchr(" \t\r", str_byte(s, to - 1), 3) != NULL) to--;
    return str_sub(s, from, to);
}

// For `edit`'s stop, where an `old` not in the file parts from it: the first
// nonblank line of `old` the file lacks, and the file's line holding that
// line's longest tail of 8 bytes or more; or that every line is there, but not
// together.
static STATUS_MUST_USE Status change_miss_hint(Run *r, Pos pos, Str file, Str old, Str *out) {
    size_t from = 0;
    while (from <= old.len) {
        size_t to = path_part_end_at(old, from, '\n');
        Str line = change_trim(str_sub(old, from, to));
        bool last_line = to == old.len;
        from = to + 1;
        if (line.len == 0 || change_index_of(file, 0, line) != SIZE_MAX) {
            if (last_line) break;
            continue;
        }
        Str cut = str_sub(line, 0, line.len < 80 ? line.len : 80);
        Buf b = buf_make(r->ctx.values);
        for (size_t s = 0; s + 8 <= line.len; s++) {
            size_t i = change_index_of(file, 0, str_sub(line, s, line.len));
            if (i == SIZE_MAX) continue;
            size_t start = builtin_line_start(file, i);
            size_t end = builtin_line_end(file, i);
            Str there = change_trim(str_sub(file, start, end));
            uint64_t no = builtin_newlines(str_sub(file, 0, start)) + 1;
            if (buf_lit(&b, ": its line \"") != OK || buf_str(&b, cut) != OK || buf_lit(&b, "\" is not in it; its line ") != OK || buf_u64(&b, no) != OK || buf_lit(&b, " ends alike: \"") != OK ||
                buf_str(&b, str_sub(there, 0, there.len < 100 ? there.len : 100)) != OK || buf_byte(&b, '"') != OK) {
                return run_values_oom(r, pos);
            }
            *out = buf_view(&b);
            return OK;
        }
        if (buf_lit(&b, ": its line \"") != OK || buf_str(&b, cut) != OK || buf_lit(&b, "\" is not in it") != OK) return run_values_oom(r, pos);
        *out = buf_view(&b);
        return OK;
    }
    *out = S(": each of its lines is in it, but not together (the blank lines or the indentation between them differ)");
    return OK;
}

// Reads `p` and applies `edits` to it in sequence, as `edit` does, charging the
// searches; writes nothing.
static STATUS_MUST_USE Status change_plan(Run *r, Pos pos, const StorePath *p, EditList edits, Plan *out) {
    Builtin b = BUILTIN_EDIT;
    TRY(builtin_count_read(r, pos));
    uint64_t size = 0;
    TRY(builtin_file_size(r, pos, b, p, &size));
    Str was = {0};
    TRY(builtin_read_sized(r, pos, b, p, size, 0, false, 0, &was));
    Spans spans = Spans_make(r->ctx.values);
    Str now = was;
    size_t replaced_n = 0;
    for (size_t k = 0; k < edits.len; k++) {
        Edit e = at(edits, k);
        Str which = S("");
        if (edits.len > 1) {
            Buf w = buf_make(r->ctx.values);
            if (buf_lit(&w, " (edit ") != OK || buf_u64(&w, k + 1) != OK || buf_lit(&w, " of ") != OK || buf_u64(&w, edits.len) != OK || buf_lit(&w, "; none is made)") != OK) return run_values_oom(r, pos);
            which = buf_view(&w);
        }
        if (e.old.len == 0) return run_failf(r, pos, CLASS_ARGUMENT, "`edit` of \"\" is an error: there is nothing to replace{s}", S("`edit` of \"\" is an error: there is nothing to replace"), which);
        // The search is charged as a scan: a step per 16 bytes.
        TRY(builtin_charge_bytes(r, pos, now.len));
        Places places = Places_make(r->ctx.values);
        size_t from = 0;
        for (;;) {
            size_t i = change_index_of(now, from, e.old);
            if (i == SIZE_MAX) break;
            from = i + e.old.len;
            bool before = i > 0 && change_word_byte(str_byte(now, i - 1));
            bool after = from < now.len && change_word_byte(str_byte(now, from));
            if (e.word && (before || after)) {
                from = i + 1;
                continue;
            }
            if (Places_push(&places, i) != OK) return run_values_oom(r, pos);
        }
        Str shown = str_sub(e.old, 0, e.old.len < 40 ? e.old.len : 40);
        if (places.len == 0) {
            Str hint = S("");
            TRY(change_miss_hint(r, pos, now, e.old, &hint));
            return run_failf(r, pos, CLASS_ARGUMENT, "`edit`: \"{s}\" is not in {s}{s}{s}{s}", S("`edit`: \""), shown, S("\" is not in "), p->rel, e.word ? S(" as a whole word") : S(""), hint, which);
        }
        if (places.len > 1 && !e.all) {
            return run_failf(r, pos, CLASS_ARGUMENT, "`edit`: \"{s}\" is in {s} {d} times: a longer `old` picks one, or `{{all = true}}` changes every one{s}", S("`edit`: \""), shown, S("\" is in "), p->rel, S(" "),
                             run_unum(r, places.len), S(" times: a longer `old` picks one, or `{all = true}` changes every one"), which);
        }
        Buf next = buf_make(r->ctx.values);
        size_t last = 0;
        for (size_t j = 0; j < places.len; j++) {
            size_t i = at(places, j);
            if (buf_str(&next, str_sub(now, last, i)) != OK || buf_str(&next, e.new_text) != OK) return run_values_oom(r, pos);
            last = i + e.old.len;
        }
        if (buf_str(&next, str_sub(now, last, now.len)) != OK) return run_values_oom(r, pos);
        TRY(builtin_charge_bytes(r, pos, next.len));
        // Spans are recorded from the last place back, so each offset still
        // refers to the text before this edit.
        for (size_t j = places.len; j > 0; j--) TRY(change_replaced(r, pos, &spans, at(places, j - 1), e.old.len, e.new_text.len));
        now = buf_view(&next);
        replaced_n += places.len;
    }
    out->p = *p;
    out->was = was;
    out->now = now;
    out->spans = Spans_items(&spans);
    out->n = replaced_n;
    return OK;
}

// Where a span's hunk ends in the old text: the newline ending the line its
// end is in; when its old and new text both end in a newline, that newline,
// so the untouched line after it stays out of the hunk.
static size_t change_hunk_end(Str was, Str now, Span s) {
    if (s.o1 > s.o0 && str_byte(was, s.o1 - 1) == '\n' && s.c1 > s.c0 && str_byte(now, s.c1 - 1) == '\n') return s.o1 - 1;
    return builtin_line_end(was, s.o1);
}

// Each line of `s`, `mark` first.
static STATUS_MUST_USE Status change_write_lines(Run *r, Pos pos, Str mark, Str s) {
    size_t from = 0;
    while (from <= s.len) {
        size_t to = path_part_end_at(s, from, '\n');
        TRY(run_write(r, pos, mark));
        TRY(run_write(r, pos, str_sub(s, from, to)));
        TRY(run_write(r, pos, S("\n")));
        if (to == s.len) break;
        from = to + 1;
    }
    return OK;
}

// Prints a unified diff of `was` becoming `now`: `--- a/p`, `+++ b/p`, then a
// hunk for each run of lines the spans touch, numbered as in the files. In
// summary the walk and its charges are the same, and the output is one line:
// `edit p: N hunks, -O +N lines`.
static STATUS_MUST_USE Status change_write_diff(Run *r, Pos pos, Str rel, Str was, Str now, SpansItems spans) {
    bool summary = builtin_store_of(r)->diffs_summary;
    uint64_t hunks = 0;
    int64_t lines_old = 0;
    int64_t lines_new = 0;
    if (!summary) {
        Buf h = buf_make(r->ctx.values);
        if (buf_lit(&h, "--- a/") != OK || buf_str(&h, rel) != OK || buf_lit(&h, "\n+++ b/") != OK || buf_str(&h, rel) != OK || buf_byte(&h, '\n') != OK) return run_values_oom(r, pos);
        TRY(run_write(r, pos, buf_view(&h)));
    }
    size_t k = 0;
    // How many more lines the new text has than the old, before this hunk.
    int64_t shift = 0;
    // The line `counted` starts, counted once across the hunks.
    int64_t line = 1;
    size_t counted = 0;
    while (k < spans.len) {
        // The bytes around a span are the same in both texts, so a hunk's
        // bounds in the old carry over to the new.
        Span s = at(spans, k);
        size_t start = builtin_line_start(was, s.o0);
        size_t now_start = s.c0 - (s.o0 - start);
        size_t end = change_hunk_end(was, now, s);
        size_t now_end = (s.c1 + end) - s.o1;
        size_t j = k + 1;
        while (j < spans.len && at(spans, j).o0 <= end) {
            end = change_hunk_end(was, now, at(spans, j));
            now_end = (at(spans, j).c1 + end) - at(spans, j).o1;
            j++;
        }
        Str before = str_sub(was, start, end);
        Str after = str_sub(now, now_start, now_end);
        line += (int64_t)builtin_newlines(str_sub(was, counted, start));
        TRY(builtin_charge_bytes(r, pos, start - counted));
        counted = start;
        int64_t n_old = (int64_t)builtin_newlines(before) + 1;
        int64_t n_new = (int64_t)builtin_newlines(after) + 1;
        TRY(run_charge(r, pos, before.len + after.len));
        if (summary) {
            hunks++;
            lines_old += n_old;
            lines_new += n_new;
        } else {
            Buf h = buf_make(r->ctx.values);
            if (buf_lit(&h, "@@ -") != OK || buf_i64(&h, line) != OK || buf_byte(&h, ',') != OK || buf_i64(&h, n_old) != OK || buf_lit(&h, " +") != OK || buf_i64(&h, line + shift) != OK || buf_byte(&h, ',') != OK ||
                buf_i64(&h, n_new) != OK || buf_lit(&h, " @@\n") != OK) {
                return run_values_oom(r, pos);
            }
            TRY(run_write(r, pos, buf_view(&h)));
            TRY(change_write_lines(r, pos, S("-"), before));
            TRY(change_write_lines(r, pos, S("+"), after));
        }
        shift += n_new - n_old;
        k = j;
    }
    if (!summary) return OK;
    Buf h = buf_make(r->ctx.values);
    if (buf_lit(&h, "edit ") != OK || buf_str(&h, rel) != OK || buf_lit(&h, ": ") != OK || buf_u64(&h, hunks) != OK || buf_lit(&h, " hunk") != OK || (hunks != 1 && buf_byte(&h, 's') != OK) || buf_lit(&h, ", -") != OK ||
        buf_i64(&h, lines_old) != OK || buf_lit(&h, " +") != OK || buf_i64(&h, lines_new) != OK || buf_lit(&h, " lines\n") != OK) {
        return run_values_oom(r, pos);
    }
    return run_write(r, pos, buf_view(&h));
}

// Asks the host whether `p` may change: every change asks before anything is
// written.
// DRAGON: the store's boundaries. A change that skipped this would write
// through a read-only mount, and past `--undo-on-stop`'s journal.
// REMEDY: `change_admit` calls it for every change, `edit`'s and
// `edits_write`'s among them, before its budgets are counted; test_builtins
// plants a write through a read-only mount.
static STATUS_MUST_USE Status change_may(Run *r, Pos pos, const StorePath *p) {
    RunStore *rs = builtin_store_of(r);
    const PathHook *h = rs->path.hook;
    if (h == NULL || h->may_change == NULL) return OK;
    return h->may_change(h->ctx, &rs->path, pos, p->rel, p->full);
}

// Everything a change asks before it is made: the host lets `p` change and the
// change budgets hold it; then it is counted, its bytes charged and its log
// entry reserved, so a change made is never one the log could not hold. A stop
// here changes nothing.
static STATUS_MUST_USE Status change_admit(Run *r, Pos pos, const StorePath *p, Str data) {
    r->tried_change = true;
    TRY(change_may(r, pos, p));
    if (r->changes >= r->ctx.budgets.changes) return run_budget_stop(&r->ctx, pos, BUDGET_CHANGES);
    if (data.len > run_sat_sub(r->ctx.budgets.change_bytes, r->change_bytes)) return run_budget_stop(&r->ctx, pos, BUDGET_CHANGE_BYTES);
    r->changes++;
    r->change_bytes += data.len;
    TRY(run_charge(r, pos, data.len));
    if (Effects_reserve(&builtin_store_of(r)->effects, 1) != OK) return run_values_oom(r, pos);
    return OK;
}

// Makes a change `change_admit` let through and logs it with its outcome; a
// failed one also stops the run. The store takes removing a missing path as
// done, and a script's removal of one is a stop, logged.
static STATUS_MUST_USE Status change_make(Run *r, Pos pos, Builtin b, ChangeKind c, const StorePath *p, Str data) {
    RunStore *rs = builtin_store_of(r);
    Arena *a = r->ctx.values;
    StoreErr e = STORE_OK;
    if (c == CHANGE_REMOVE || c == CHANGE_REMOVE_TREE) {
        bool found = false;
        StoreStat st;
        e = rs->store.stat_or_null(rs->store.ctx, a, p->full, &found, &st);
        if (e == STORE_OK && !found) e = STORE_FILE_NOT_FOUND;
    }
    if (e == STORE_OK) {
        uint64_t size = 0;
        switch (c) {
        case CHANGE_REPLACE: e = rs->store.replace(rs->store.ctx, a, p->full, data); break;
        case CHANGE_APPEND: e = rs->store.append(rs->store.ctx, a, p->full, data, &size); break;
        case CHANGE_MKDIR: e = rs->store.make_dir(rs->store.ctx, a, p->full); break;
        case CHANGE_REMOVE: e = rs->store.remove(rs->store.ctx, a, p->full); break;
        case CHANGE_REMOVE_TREE: e = rs->store.remove_tree(rs->store.ctx, a, p->full); break;
        }
    }
    Effect logged = {builtin_name(b), p->rel, e == STORE_OK ? S("ok") : store_err_name(e)};
    // The room was reserved by change_admit, so this push cannot be refused.
    if (Effects_push(&rs->effects, logged) != OK) return run_values_oom(r, pos);
    if (e != STORE_OK) return builtin_store_failed(r, pos, b, p->rel, e);
    return OK;
}

// A change of kind `c` to `p` with `data`, logged under `b`'s name.
static STATUS_MUST_USE Status change_at(Run *r, Pos pos, Builtin b, ChangeKind c, const StorePath *p, Str data) {
    TRY(change_admit(r, pos, p, data));
    return change_make(r, pos, b, c, p, data);
}

static STATUS_MUST_USE Status change_store(Run *r, Pos pos, Builtin b, ChangeKind c, ConstValues args, Value *out) {
    StorePath p;
    TRY(builtin_path(r, pos, b, &at(args, 0), false, &p));
    Str data = S("");
    if (c == CHANGE_REPLACE || c == CHANGE_APPEND) TRY(builtin_string_arg(r, pos, b, &at(args, 1), S("what it writes"), &data));
    TRY(change_at(r, pos, b, c, &p, data));
    value_nil(out);
    return OK;
}

static STATUS_MUST_USE Status bi_replace(Run *r, Pos pos, ConstValues args, Value *out) {
    return change_store(r, pos, BUILTIN_REPLACE, CHANGE_REPLACE, args, out);
}

static STATUS_MUST_USE Status bi_append(Run *r, Pos pos, ConstValues args, Value *out) {
    return change_store(r, pos, BUILTIN_APPEND, CHANGE_APPEND, args, out);
}

static STATUS_MUST_USE Status bi_mkdir(Run *r, Pos pos, ConstValues args, Value *out) {
    return change_store(r, pos, BUILTIN_MKDIR, CHANGE_MKDIR, args, out);
}

static STATUS_MUST_USE Status bi_remove(Run *r, Pos pos, ConstValues args, Value *out) {
    return change_store(r, pos, BUILTIN_REMOVE, CHANGE_REMOVE, args, out);
}

static STATUS_MUST_USE Status bi_remove_tree(Run *r, Pos pos, ConstValues args, Value *out) {
    return change_store(r, pos, BUILTIN_REMOVE_TREE, CHANGE_REMOVE_TREE, args, out);
}

// `edit(p, old, new[, {all, word}])` or `edit(p, {{old, new}, ...})`: the
// edits made in sequence, the diff printed, the file written once, logged as
// `edit`; the replacements made. An edit that fails stops the run before
// anything is printed or written.
static STATUS_MUST_USE Status bi_edit(Run *r, Pos pos, ConstValues args, Value *out) {
    StorePath p;
    TRY(builtin_path(r, pos, BUILTIN_EDIT, &at(args, 0), false, &p));
    EditList edits;
    TRY(change_edits_arg(r, pos, args, &edits));
    Plan plan;
    TRY(change_plan(r, pos, &p, edits, &plan));
    TRY(change_write_diff(r, pos, p.rel, plan.was, plan.now, plan.spans));
    TRY(change_at(r, pos, BUILTIN_EDIT, CHANGE_REPLACE, &p, plan.now));
    value_int(out, (int64_t)plan.n);
    return OK;
}

// `edit` over several files as one (the Linux host's `edits_write`, under the
// prelude's `edits_all`): `list` is `{{p, {edits}}, ...}`. Every file is
// planned, every diff printed and every change admitted before any is written,
// so a stop of any kind leaves every file as it was; only the store failing a
// write after the first can part them, and the effects say which were made. A
// file named twice is refused.
static STATUS_MUST_USE Status change_edits_write(Run *r, Pos pos, const Value *list, Value *out) {
    Builtin b = BUILTIN_EDIT;
    Table *t = NULL;
    TRY(builtin_list_arg(r, pos, b, list, &t));
    size_t count = t->list.len;
    Plan *plans_ptr = ARENA_ARRAY(r->ctx.values, Plan, count);
    if (count > 0 && plans_ptr == NULL) return run_values_oom(r, pos);
    SLICE_TYPE(Plans, Plan);
    Plans plans = {plans_ptr, count};
    for (size_t k = 0; k < count; k++) {
        const Value *item = &at(t->list, k);
        bool pair = item->tag == VALUE_TABLE && item->as.table->kind == TABLE_LIST && item->as.table->list.len == 2;
        if (!pair) {
            return run_failf(r, pos, CLASS_TYPE, "`edits_write`: item {d} must be `{{p, {{edits}}}}`, not {s}", S("`edits_write`: item "), run_unum(r, k + 1), S(" must be `{p, {edits}}`, not "), value_type_name(item));
        }
        ValueVecItems two = ValueVec_items(&item->as.table->list);
        StorePath p;
        TRY(builtin_path(r, pos, b, &at(two, 0), false, &p));
        for (size_t j = 0; j < k; j++) {
            if (str_eq(at(plans, j).p.full, p.full)) {
                return run_failf(r, pos, CLASS_ARGUMENT, "`edits_write`: {s} is named twice: one list of its edits, which apply in sequence", S("`edits_write`: "), p.rel, S(" is named twice: one list of its edits, which apply in sequence"));
            }
        }
        // `edit`'s own arguments, the path and the list.
        Value pair_args[2] = {at(two, 0), at(two, 1)};
        ConstValues edit_args = {pair_args, 2};
        EditList edits;
        TRY(change_edits_arg(r, pos, edit_args, &edits));
        TRY(change_plan(r, pos, &p, edits, &at(plans, k)));
    }
    for (size_t k = 0; k < count; k++) TRY(change_write_diff(r, pos, at(plans, k).p.rel, at(plans, k).was, at(plans, k).now, at(plans, k).spans));
    // Every file's log entry at once, so no write waits on the arena.
    if (Effects_reserve(&builtin_store_of(r)->effects, count) != OK) return run_values_oom(r, pos);
    for (size_t k = 0; k < count; k++) TRY(change_admit(r, pos, &at(plans, k).p, at(plans, k).now));
    size_t n = 0;
    for (size_t k = 0; k < count; k++) {
        TRY(change_make(r, pos, b, CHANGE_REPLACE, &at(plans, k).p, at(plans, k).now));
        n += at(plans, k).n;
    }
    value_int(out, (int64_t)n);
    return OK;
}
