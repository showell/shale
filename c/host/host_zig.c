// host_zig.c: the host's Zig tools, `zig_rename`, `zig_decls`, `zig_tokens`,
// `zig_move` and `zig_imports`. It holds no Zig grammar: it checks a
// call's arguments, reads the files through the file system, asks the run's
// zig-ast (zig_ast.c), and acts on the answer in order: its charges, its
// stops (each message said here), the values it gives the
// script, and the edits, written as `edit`'s are, so each change is diffed
// and logged. zig-ast writes no file.
//
// Include zig_ast.c first.

// ── zig_rename ──

// A rename's scope beyond a plain one's: a container's member, or
// `prefix.from` through the given prefixes.
typedef enum {
    HOST_ZIG_SCOPE_NONE,
    HOST_ZIG_SCOPE_CONTAINER,
    HOST_ZIG_SCOPE_VIA,
} HostZigScopeKind;

VEC_DEFINE(HostZigStrs, Str)

typedef struct {
    // `lines`' first and last, each 1 or more; `first` 0 for no `lines`.
    size_t first;
    size_t last;
    HostZigScopeKind scope;
    Str container;
    HostZigStrsItems via;
} HostZigRenameOpts;

// Whether every part of `s` split at `.` is an identifier.
static bool host_zig_dotted(Str s) {
    size_t from = 0;
    for (size_t i = 0; i <= s.len; i++) {
        if (i < s.len && at(s, i) != '.') continue;
        if (!zig_wire_valid_id(str_sub(s, from, i))) return false;
        from = i + 1;
    }
    return true;
}

// `zig_rename`'s `lines`, from a map of the options but `container` and
// `via`.
static STATUS_MUST_USE Status host_zig_lines_opt(Run *r, Pos pos, Table *m, HostZigRenameOpts *o) {
    for (size_t k = 0; k < ValueMap_len(&m->map); k++) {
        Str key = ValueMap_key_at(&m->map, k);
        Value x = ValueMap_value_at(&m->map, k);
        if (!str_eq(key, S("lines"))) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_rename` takes `lines`, `container` and `via`, not `{s}`", S("`zig_rename` takes `lines`, `container` and `via`, not `"), key, S("`"));
        if (x.tag != VALUE_TABLE) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_rename`'s `lines` is `{{first, last}}`", S("`zig_rename`'s `lines` is `{first, last}`"));
        Table *l = x.as.table;
        bool pair = l->kind == TABLE_LIST && l->list.len == 2 && at(l->list, 0).tag == VALUE_INT && at(l->list, 1).tag == VALUE_INT;
        if (!pair) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_rename`'s `lines` is `{{first, last}}`", S("`zig_rename`'s `lines` is `{first, last}`"));
        int64_t first = at(l->list, 0).as.integer;
        int64_t last = at(l->list, 1).as.integer;
        if (first < 1 || last < first) {
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_rename`'s `lines` ({d}, {d}) is no range: 1 <= first <= last", S("`zig_rename`'s `lines` ("), run_num(r, first), S(", "), run_num(r, last), S(") is no range: 1 <= first <= last"));
        }
        o->first = (size_t)first;
        o->last = (size_t)last;
    }
    return OK;
}

// `zig_rename`'s options, Zig's `renameOpts`: `lines`, `container`, `via` (a
// prefix or a list).
static STATUS_MUST_USE Status host_zig_rename_opts(Run *r, Pos pos, const Value *v, HostZigRenameOpts *o) {
    memset(o, 0, sizeof *o);
    if (v->tag == VALUE_NIL) return OK;
    if (v->tag != VALUE_TABLE) return run_failf(r, pos, CLASS_TYPE, "`zig_rename`'s options are a map, not {s}", S("`zig_rename`'s options are a map, not "), value_type_name(v));
    Table *m = v->as.table;
    if (m->kind == TABLE_LIST) return run_fail(&r->ctx, pos, CLASS_TYPE, S("`zig_rename`'s options are a map, not a list"));
    Table *only_lines = NULL;
    TRY(host_zig_map(r, pos, &only_lines));
    for (size_t k = 0; k < ValueMap_len(&m->map); k++) {
        Str key = ValueMap_key_at(&m->map, k);
        Value x = ValueMap_value_at(&m->map, k);
        if (str_eq(key, S("container"))) {
            if (o->scope != HOST_ZIG_SCOPE_NONE) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`zig_rename` takes `container` or `via`, not both"));
            Str c = {0};
            TRY(builtin_string_arg(r, pos, BUILTIN_HOST, &x, S("`container`"), &c));
            if (!zig_wire_valid_id(c)) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_rename`'s `container` (\"{s}\") is not a Zig identifier", S("`zig_rename`'s `container` (\""), c, S("\") is not a Zig identifier"));
            o->scope = HOST_ZIG_SCOPE_CONTAINER;
            o->container = c;
        } else if (str_eq(key, S("via"))) {
            if (o->scope != HOST_ZIG_SCOPE_NONE) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`zig_rename` takes `container` or `via`, not both"));
            HostZigStrs list = HostZigStrs_make(r->ctx.values);
            if (x.tag == VALUE_STRING) {
                if (HostZigStrs_push(&list, x.as.string) != OK) return run_values_oom(r, pos);
            } else if (x.tag == VALUE_TABLE) {
                for (size_t i = 0; i < x.as.table->list.len; i++) {
                    Str pre = {0};
                    TRY(builtin_string_arg(r, pos, BUILTIN_HOST, &at(x.as.table->list, i), S("a `via` prefix"), &pre));
                    if (HostZigStrs_push(&list, pre) != OK) return run_values_oom(r, pos);
                }
            } else {
                return run_failf(r, pos, CLASS_TYPE, "`zig_rename`'s `via` is a prefix (\"search_index\") or a list of them, not {s}", S("`zig_rename`'s `via` is a prefix (\"search_index\") or a list of them, not "), value_type_name(&x));
            }
            if (list.len == 0) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`zig_rename`'s `via` is empty: give the prefixes the callers use"));
            for (size_t i = 0; i < list.len; i++) {
                Str pre = at(list, i);
                if (!host_zig_dotted(pre)) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_rename`'s `via` prefix \"{s}\" is not names joined by `.`", S("`zig_rename`'s `via` prefix \""), pre, S("\" is not names joined by `.`"));
            }
            o->scope = HOST_ZIG_SCOPE_VIA;
            o->via = HostZigStrs_items(&list);
        } else {
            TRY(run_map_put(r, pos, only_lines, key, &x));
        }
    }
    return host_zig_lines_opt(r, pos, only_lines, o);
}

typedef struct {
    Str from;
    Str to;
} HostZigRename;

VEC_DEFINE(HostZigRenames, HostZigRename)
VEC_DEFINE(HostZigSizes, size_t)

static STATUS_MUST_USE Status host_zig_names_type(Run *r, Pos pos) {
    return run_failf(r, pos, CLASS_TYPE, "`zig_rename`'s names are `{{from, to}}`", S("`zig_rename`'s names are `{from, to}`"));
}

// `zig_renames`' two stops on its map of names, apart so a test reaches them
// past the path, which the values' arena must give.
static STATUS_MUST_USE Status host_zig_renames_list(Run *r, Pos pos) {
    return run_failf(r, pos, CLASS_TYPE, "`zig_renames` takes a map of names, `{{a = \"x.a\"}}`, not a list", S("`zig_renames` takes a map of names, `{a = \"x.a\"}`, not a list"));
}

static STATUS_MUST_USE Status host_zig_renames_none(Run *r, Pos pos) {
    return run_failf(r, pos, CLASS_ARGUMENT, "`zig_renames` of no names: give `{{a = \"x.a\", ...}}`", S("`zig_renames` of no names: give `{a = \"x.a\", ...}`"));
}

// `host("zig_rename", path, {from, to}, opts)`: reads, plans through zig-ast
// and writes through `edit`'s tail if anything is renamed. Gives
// `{renamed, left_in_comments}` (and `left` with `via`). `{many = {a =
// "x.a"}}` in place of `{from, to}` is `zig_renames`.
static STATUS_MUST_USE Status host_zig_rename_in(Paths *h, HostZigProc *z, Arena *sa, Run *r, Pos pos, ConstValues args, Value *out) {
    r->tried_change = true;
    if (args.len != 3) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_rename` takes a path, `{{from, to}}` and options, given {d}", S("`zig_rename` takes a path, `{from, to}` and options, given "), run_unum(r, args.len));
    CheckedPath p;
    TRY(builtin_checked_path(r, pos, BUILTIN_HOST, &at(args, 0), false, &p));
    const Value *nv = &at(args, 1);
    if (nv->tag != VALUE_TABLE || nv->as.table->kind != TABLE_MAP) return host_zig_names_type(r, pos);
    Table *names = nv->as.table;
    HostZigRenames renames = HostZigRenames_make(r->ctx.values);
    Value many;
    if (ValueMap_get(&names->map, S("many"), &many)) {
        if (many.tag != VALUE_TABLE) return run_failf(r, pos, CLASS_TYPE, "`zig_renames` takes a map of names, `{{a = \"x.a\"}}`, not {s}", S("`zig_renames` takes a map of names, `{a = \"x.a\"}`, not "), value_type_name(&many));
        Table *m = many.as.table;
        if (m->kind == TABLE_LIST) return host_zig_renames_list(r, pos);
        for (size_t k = 0; k < ValueMap_len(&m->map); k++) {
            Value v = ValueMap_value_at(&m->map, k);
            HostZigRename one = {ValueMap_key_at(&m->map, k), {0}};
            TRY(builtin_string_arg(r, pos, BUILTIN_HOST, &v, S("a name's `to`"), &one.to));
            if (HostZigRenames_push(&renames, one) != OK) return run_values_oom(r, pos);
        }
        if (renames.len == 0) return host_zig_renames_none(r, pos);
    } else {
        Value from;
        Value to;
        if (!ValueMap_get(&names->map, S("from"), &from)) value_nil(&from);
        if (!ValueMap_get(&names->map, S("to"), &to)) value_nil(&to);
        HostZigRename one = {{0}, {0}};
        TRY(builtin_string_arg(r, pos, BUILTIN_HOST, &from, S("`from`"), &one.from));
        TRY(builtin_string_arg(r, pos, BUILTIN_HOST, &to, S("`to`"), &one.to));
        if (HostZigRenames_push(&renames, one) != OK) return run_values_oom(r, pos);
    }
    for (size_t k = 0; k < renames.len; k++) {
        HostZigRename one = at(renames, k);
        if (!zig_wire_valid_id(one.from)) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_rename`'s `from` (\"{s}\") is not a Zig identifier: one name, not a keyword", S("`zig_rename`'s `from` (\""), one.from, S("\") is not a Zig identifier: one name, not a keyword"));
        if (!host_zig_dotted(one.to)) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_rename`'s `to` (\"{s}\") is not a Zig identifier, or names joined by `.`", S("`zig_rename`'s `to` (\""), one.to, S("\") is not a Zig identifier, or names joined by `.`"));
    }
    HostZigRenameOpts o;
    TRY(host_zig_rename_opts(r, pos, &at(args, 2), &o));
    if (o.scope != HOST_ZIG_SCOPE_NONE && (renames.len != 1 || o.first != 0)) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`zig_rename`'s `container` or `via` takes one name and no `lines`"));
    Str was = {0};
    TRY(host_zig_read(r, pos, &p, &was));
    Buf req = buf_make(sa);
    TRY(host_zig_req_line(r, pos, &req, S("rename"), HOST_ZIG_NONE, HOST_ZIG_NONE));
    for (size_t k = 0; k < renames.len; k++) {
        Status s = zig_wire_field(&req, S("name"));
        if (s == OK) s = zig_wire_tab(&req);
        if (s == OK) s = zig_wire_field(&req, at(renames, k).from);
        if (s == OK) s = zig_wire_tab(&req);
        if (s == OK) s = zig_wire_field(&req, at(renames, k).to);
        if (s == OK) s = zig_wire_end(&req);
        TRY(host_zig_req_ok(r, pos, s));
    }
    if (o.first != 0) {
        Status s = buf_lit(&req, "lines\t");
        if (s == OK) s = buf_u64(&req, o.first);
        if (s == OK) s = zig_wire_tab(&req);
        if (s == OK) s = buf_u64(&req, o.last);
        if (s == OK) s = zig_wire_end(&req);
        TRY(host_zig_req_ok(r, pos, s));
    }
    if (o.scope == HOST_ZIG_SCOPE_CONTAINER) TRY(host_zig_req_line(r, pos, &req, S("container"), o.container, HOST_ZIG_NONE));
    for (size_t k = 0; o.scope == HOST_ZIG_SCOPE_VIA && k < o.via.len; k++) TRY(host_zig_req_line(r, pos, &req, S("via"), at(o.via, k), HOST_ZIG_NONE));
    TRY(host_zig_req_ok(r, pos, zig_wire_source(&req, was)));
    HostZigAnswer an;
    TRY(host_zig_ask(z, h, sa, r, pos, r->host_call, buf_view(&req), &an));
    size_t renamed = 0;
    size_t left_in_comments = 0;
    HostZigEdits edits = HostZigEdits_make(sa);
    HostZigSizes left = HostZigSizes_make(sa);
    for (;;) {
        bool got = false;
        TRY(host_zig_next(&an, &got));
        if (!got) break;
        size_t n = 0;
        if (host_zig_is(&an, S("work"))) {
            TRY(host_zig_work(&an));
        } else if (host_zig_is(&an, S("renamed"))) {
            TRY(host_zig_int(&an, 1, &renamed));
        } else if (host_zig_is(&an, S("left_in_comments"))) {
            TRY(host_zig_int(&an, 1, &left_in_comments));
        } else if (host_zig_is(&an, S("s"))) {
            HostZigEdit e;
            TRY(host_zig_edit_of(&an, &e));
            if (HostZigEdits_push(&edits, e) != OK) return run_values_oom(r, pos);
        } else if (host_zig_is(&an, S("left"))) {
            TRY(host_zig_int(&an, 1, &n));
            if (HostZigSizes_push(&left, n) != OK) return run_values_oom(r, pos);
        } else if (host_zig_is(&an, S("no_container"))) {
            TRY(host_zig_int(&an, 1, &n));
            Str c = o.container;
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_rename`: {s} declares no container `{s}` (`const {s} = struct {{...}}`){s}; nothing written",
                             S("`zig_rename`: "), p.rel, S(" declares no container `"), c, S("` (`const "), c, S(" = struct {...}`)"), n == 2 ? S(", or declares it twice") : S(""), S("; nothing written"));
        } else if (host_zig_is(&an, S("no_member"))) {
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_rename`: {s}'s `{s}` declares no member `{s}`; nothing written",
                             S("`zig_rename`: "), p.rel, S("'s `"), o.container, S("` declares no member `"), at(renames, 0).from, S("`; nothing written"));
        } else if (host_zig_is(&an, S("unattributed"))) {
            TRY(host_zig_int(&an, 1, &n));
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_rename`: {s}:{d} has a `.{s}` that may or may not be `{s}`'s member: rename that one yourself, or rename it apart first; nothing written",
                             S("`zig_rename`: "), p.rel, S(":"), run_unum(r, n), S(" has a `."), at(renames, 0).from, S("` that may or may not be `"), o.container, S("`'s member: rename that one yourself, or rename it apart first; nothing written"));
        } else if (host_zig_is(&an, S("declaration"))) {
            Str name = {0};
            TRY(host_zig_field(&an, 2, &name));
            Str to = S("");
            for (size_t k = 0; k < renames.len; k++) {
                if (str_eq(at(renames, k).from, name)) to = at(renames, k).to;
            }
            TRY(host_zig_int(&an, 1, &n));
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_rename`: {s} declares `{s}` at line {d}, and `{s}` is qualified, which cannot be declared: rename the declaration itself, or leave it out with `lines`; nothing written",
                             S("`zig_rename`: "), p.rel, S(" declares `"), name, S("` at line "), run_unum(r, n), S(", and `"), to, S("` is qualified, which cannot be declared: rename the declaration itself, or leave it out with `lines`; nothing written"));
        } else if (host_zig_is(&an, S("invalid"))) {
            TRY(host_zig_int(&an, 1, &n));
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_rename`: {s} does not tokenize as Zig at line {d}; nothing written", S("`zig_rename`: "), p.rel, S(" does not tokenize as Zig at line "), run_unum(r, n), S("; nothing written"));
        } else {
            return host_zig_wrong(&an);
        }
    }
    if (renamed > 0) {
        Str now = {0};
        SpansItems spans;
        TRY(host_zig_apply(&an, was, HostZigEdits_items(&edits), &now, &spans));
        TRY(host_zig_rewrite(r, pos, &p, was, now, spans));
    }
    Table *t = NULL;
    TRY(host_zig_map(r, pos, &t));
    TRY(run_map_put_int(r, pos, t, S("renamed"), (int64_t)renamed));
    TRY(run_map_put_int(r, pos, t, S("left_in_comments"), (int64_t)left_in_comments));
    if (o.scope == HOST_ZIG_SCOPE_VIA) {
        Table *lt = NULL;
        TRY(host_zig_list(r, pos, &lt));
        for (size_t k = 0; k < left.len; k++) {
            Value v;
            value_int(&v, (int64_t)at(left, k));
            TRY(run_list_push(r, pos, lt, &v));
        }
        TRY(host_zig_put_table(r, pos, t, S("left"), lt));
    }
    value_table(out, t);
    return OK;
}

// ── zig_decls ──

// `host("zig_decls", path[, {container}])`: a Zig file's top-level
// declarations (or that container's members) as maps, in file order. A
// parse error stops the run at its first line.
static STATUS_MUST_USE Status host_zig_decls_in(Paths *h, HostZigProc *z, Arena *sa, Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len < 1 || args.len > 2) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_decls` takes a path and options, given {d} arguments", S("`zig_decls` takes a path and options, given "), run_unum(r, args.len), S(" arguments"));
    bool has_container = false;
    Str container = S("");
    if (args.len == 2) {
        const Value *v = &at(args, 1);
        if (v->tag != VALUE_TABLE) return run_failf(r, pos, CLASS_TYPE, "`zig_decls`' options are a map, not {s}", S("`zig_decls`' options are a map, not "), value_type_name(v));
        Table *t = v->as.table;
        if (t->kind != TABLE_MAP) return run_fail(&r->ctx, pos, CLASS_TYPE, S("`zig_decls`' options are a map, not a list"));
        for (size_t k = 0; k < ValueMap_len(&t->map); k++) {
            Str key = ValueMap_key_at(&t->map, k);
            Value x = ValueMap_value_at(&t->map, k);
            if (!str_eq(key, S("container"))) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_decls` takes `container`, not `{s}`", S("`zig_decls` takes `container`, not `"), key, S("`"));
            if (x.tag != VALUE_STRING) return run_failf(r, pos, CLASS_TYPE, "`zig_decls`' `container` is a name, not {s}", S("`zig_decls`' `container` is a name, not "), value_type_name(&x));
            has_container = true;
            container = x.as.string;
        }
    }
    CheckedPath p;
    TRY(builtin_checked_path(r, pos, BUILTIN_HOST, &at(args, 0), false, &p));
    Str src = {0};
    TRY(host_zig_read(r, pos, &p, &src));
    Buf req = buf_make(sa);
    TRY(host_zig_req_line(r, pos, &req, S("decls"), HOST_ZIG_NONE, HOST_ZIG_NONE));
    if (has_container) TRY(host_zig_req_line(r, pos, &req, S("container"), container, HOST_ZIG_NONE));
    TRY(host_zig_req_ok(r, pos, zig_wire_source(&req, src)));
    HostZigAnswer an;
    TRY(host_zig_ask(z, h, sa, r, pos, r->host_call, buf_view(&req), &an));
    Table *list = NULL;
    TRY(host_zig_list(r, pos, &list));
    // The declaration whose `u` and `r` lines come next.
    Table *uses = NULL;
    Table *reaches = NULL;
    for (;;) {
        bool got = false;
        TRY(host_zig_next(&an, &got));
        if (!got) break;
        size_t n = 0;
        if (host_zig_is(&an, S("work"))) {
            TRY(host_zig_work(&an));
        } else if (host_zig_is(&an, S("d"))) {
            if (an.l.len != 12) return host_zig_wrong(&an);
            Table *m = NULL;
            TRY(host_zig_map(r, pos, &m));
            static const Str text_keys_all[] = {S_ITEM("kind"), S_ITEM("name"), S_ITEM("name_form"), S_ITEM("visibility"), S_ITEM("linkage")};
            SLICE_TYPE(HostZigKeys, const Str);
            HostZigKeys text_keys = {text_keys_all, sizeof text_keys_all / sizeof *text_keys_all};
            for (size_t i = 0; i < text_keys.len; i++) TRY(host_zig_put_kept(r, pos, m, at(text_keys, i), at(an.l, i + 1)));
            static const Str int_keys_all[] = {S_ITEM("first"), S_ITEM("last"), S_ITEM("start"), S_ITEM("stop")};
            HostZigKeys int_keys = {int_keys_all, sizeof int_keys_all / sizeof *int_keys_all};
            for (size_t i = 0; i < int_keys.len; i++) {
                TRY(host_zig_int(&an, 6 + i, &n));
                TRY(run_map_put_int(r, pos, m, at(int_keys, i), (int64_t)n));
            }
            size_t above_first = 0;
            TRY(host_zig_int(&an, 10, &above_first));
            if (above_first > 0) {
                Table *cm = NULL;
                TRY(host_zig_map(r, pos, &cm));
                TRY(run_map_put_int(r, pos, cm, S("first"), (int64_t)above_first));
                TRY(host_zig_int(&an, 11, &n));
                TRY(run_map_put_int(r, pos, cm, S("last"), (int64_t)n));
                TRY(host_zig_put_table(r, pos, m, S("comment_above"), cm));
            }
            TRY(host_zig_list(r, pos, &uses));
            TRY(host_zig_put_table(r, pos, m, S("uses"), uses));
            if (has_container) {
                TRY(host_zig_list(r, pos, &reaches));
                TRY(host_zig_put_table(r, pos, m, S("reaches"), reaches));
            }
            TRY(host_zig_push_table(r, pos, list, m));
        } else if (host_zig_is(&an, S("u")) || host_zig_is(&an, S("r"))) {
            Table *into = host_zig_is(&an, S("u")) ? uses : reaches;
            if (into == NULL) return host_zig_wrong(&an);
            Str f = {0};
            TRY(host_zig_field(&an, 1, &f));
            Str kept = {0};
            TRY(host_zig_keep(r, pos, f, &kept));
            TRY(builtin_push_str(r, pos, into, kept));
        } else if (host_zig_is(&an, S("parse_error"))) {
            TRY(host_zig_int(&an, 1, &n));
            Str why = {0};
            TRY(host_zig_field(&an, 2, &why));
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_decls`: {s} does not parse as Zig: line {d} ({s})", S("`zig_decls`: "), p.rel, S(" does not parse as Zig: line "), run_unum(r, n), S(" ("), why, S(")"));
        } else if (host_zig_is(&an, S("no_container"))) {
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_decls`: {s} has no container `{s}` (a top-level `const {s} = struct {{...}}`, or a union's, an enum's, an opaque's)",
                             S("`zig_decls`: "), p.rel, S(" has no container `"), container, S("` (a top-level `const "), container, S(" = struct {...}`, or a union's, an enum's, an opaque's)"));
        } else if (host_zig_is(&an, S("invalid"))) {
            TRY(host_zig_int(&an, 1, &n));
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_decls`: {s} does not tokenize as Zig at line {d}", S("`zig_decls`: "), p.rel, S(" does not tokenize as Zig at line "), run_unum(r, n));
        } else {
            return host_zig_wrong(&an);
        }
    }
    value_table(out, list);
    return OK;
}

// ── zig_tokens ──

// `host("zig_tokens", path[, {tag}])`: the file's tokens as `{tag, text,
// line}`, `tag` a std.zig.Token.Tag name; `{tag = name}` keeps one kind.
static STATUS_MUST_USE Status host_zig_tokens_in(Paths *h, HostZigProc *z, Arena *sa, Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len < 1 || args.len > 2) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_tokens` takes a path and options, given {d} arguments", S("`zig_tokens` takes a path and options, given "), run_unum(r, args.len), S(" arguments"));
    bool has_only = false;
    Str only = S("");
    if (args.len == 2) {
        const Value *v = &at(args, 1);
        if (v->tag != VALUE_TABLE) return run_failf(r, pos, CLASS_TYPE, "`zig_tokens`' options are a map, not {s}", S("`zig_tokens`' options are a map, not "), value_type_name(v));
        Table *t = v->as.table;
        if (t->kind != TABLE_MAP) return run_fail(&r->ctx, pos, CLASS_TYPE, S("`zig_tokens`' options are a map, not a list"));
        for (size_t k = 0; k < ValueMap_len(&t->map); k++) {
            Str key = ValueMap_key_at(&t->map, k);
            Value x = ValueMap_value_at(&t->map, k);
            if (!str_eq(key, S("tag"))) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_tokens` takes `tag`, not `{s}`", S("`zig_tokens` takes `tag`, not `"), key, S("`"));
            if (x.tag != VALUE_STRING) return run_failf(r, pos, CLASS_TYPE, "`zig_tokens`' `tag` is a tag's name, not {s}", S("`zig_tokens`' `tag` is a tag's name, not "), value_type_name(&x));
            has_only = true;
            only = x.as.string;
        }
    }
    CheckedPath p;
    TRY(builtin_checked_path(r, pos, BUILTIN_HOST, &at(args, 0), false, &p));
    Str src = {0};
    TRY(host_zig_read(r, pos, &p, &src));
    Buf req = buf_make(sa);
    TRY(host_zig_req_line(r, pos, &req, S("tokens"), HOST_ZIG_NONE, HOST_ZIG_NONE));
    if (has_only) TRY(host_zig_req_line(r, pos, &req, S("tag"), only, HOST_ZIG_NONE));
    TRY(host_zig_req_ok(r, pos, zig_wire_source(&req, src)));
    HostZigAnswer an;
    TRY(host_zig_ask(z, h, sa, r, pos, r->host_call, buf_view(&req), &an));
    Table *list = NULL;
    TRY(host_zig_list(r, pos, &list));
    // Each tag's name, by its number, as the answer names it.
    Str names_room[256];
    bool named_room[256];
    SLICE_TYPE(HostZigTagNames, Str);
    SLICE_TYPE(HostZigTagNamed, bool);
    HostZigTagNames names = {names_room, 256};
    HostZigTagNamed named = {named_room, 256};
    memset(named_room, 0, sizeof named_room);
    for (;;) {
        bool got = false;
        TRY(host_zig_next(&an, &got));
        if (!got) break;
        size_t n = 0;
        if (host_zig_is(&an, S("work"))) {
            TRY(host_zig_work(&an));
        } else if (host_zig_is(&an, S("tag"))) {
            TRY(host_zig_int(&an, 1, &n));
            if (n >= names.len) return host_zig_wrong(&an);
            Str f = {0};
            TRY(host_zig_field(&an, 2, &f));
            TRY(host_zig_keep(r, pos, f, &at(names, n)));
            at(named, n) = true;
        } else if (host_zig_is(&an, S("tb"))) {
            TRY(host_zig_int(&an, 1, &n));
            Str block = S("");
            if (n > SIZE_MAX / ZIG_WIRE_TOKEN_BYTES || !zig_wire_block(&an.lines, n * ZIG_WIRE_TOKEN_BYTES, &block)) return host_zig_wrong(&an);
            if (ValueVec_reserve(&list->list, n) != OK) return run_values_oom(r, pos);
            for (size_t i = 0; i < n; i++) {
                ZigWireToken tk;
                zig_wire_token(block, i, &tk);
                if (tk.end < tk.start || tk.end > src.len) return host_zig_wrong(&an);
                if (!at(named, tk.tag)) return host_zig_wrong(&an);
                Table *m = NULL;
                TRY(host_zig_map(r, pos, &m));
                TRY(run_map_put_str(r, pos, m, S("tag"), at(names, tk.tag)));
                // The token's text points into the file's bytes, which are
                // the run's values already: Zig copies it, to the same effect.
                TRY(run_map_put_str(r, pos, m, S("text"), str_sub(src, tk.start, tk.end)));
                TRY(run_map_put_int(r, pos, m, S("line"), tk.line));
                TRY(host_zig_push_table(r, pos, list, m));
            }
        } else if (host_zig_is(&an, S("no_tag"))) {
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_tokens`: no token tag `{s}` (std.zig.Token.Tag: `identifier`, `keyword_fn`, `string_literal`, ...)", S("`zig_tokens`: no token tag `"), only, S("` (std.zig.Token.Tag: `identifier`, `keyword_fn`, `string_literal`, ...)"));
        } else if (host_zig_is(&an, S("invalid"))) {
            TRY(host_zig_int(&an, 1, &n));
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_tokens`: {s} does not tokenize as Zig at line {d}", S("`zig_tokens`: "), p.rel, S(" does not tokenize as Zig at line "), run_unum(r, n));
        } else {
            return host_zig_wrong(&an);
        }
    }
    value_table(out, list);
    return OK;
}

// ── zig_move ──

// A name and its line, as a move reports it; `target` for an import.
typedef struct {
    Str name;
    Str target;
    size_t line;
} HostZigNameLine;

VEC_DEFINE(HostZigNameLines, HostZigNameLine)

// A list of `{name, line}` maps, or `{name, target, line}` with `targets`,
// put in `t` under `k`.
static STATUS_MUST_USE Status host_zig_name_lines(Run *r, Pos pos, Table *t, Str k, HostZigNameLinesItems items, bool targets) {
    Table *list = NULL;
    TRY(host_zig_list(r, pos, &list));
    for (size_t i = 0; i < items.len; i++) {
        HostZigNameLine x = at(items, i);
        Table *m = NULL;
        TRY(host_zig_map(r, pos, &m));
        TRY(host_zig_put_kept(r, pos, m, S("name"), x.name));
        if (targets) TRY(host_zig_put_kept(r, pos, m, S("target"), x.target));
        TRY(run_map_put_int(r, pos, m, S("line"), (int64_t)x.line));
        TRY(host_zig_push_table(r, pos, list, m));
    }
    return host_zig_put_table(r, pos, t, k, list);
}

// A name to move, and its kind when one name is two.
typedef struct {
    Str name;
    Str kind;
} HostZigWant;

VEC_DEFINE(HostZigWants, HostZigWant)

// The groups of a move's edits, in the order Zig's `Group` names them.
typedef enum {
    HOST_ZIG_TO,
    HOST_ZIG_FROM,
    HOST_ZIG_TO_Q,
    HOST_ZIG_TO_I,
    HOST_ZIG_FROM_P,
    HOST_ZIG_GROUPS,
} HostZigGroup;

// The lists a move reports, in the order Zig's `Report` names them.
typedef enum {
    HOST_ZIG_NEEDS_PUB,
    HOST_ZIG_NEEDS_IMPORT,
    HOST_ZIG_NEEDS_FROM_FROM,
    HOST_ZIG_USES_LEFT,
    HOST_ZIG_REPORTS,
} HostZigReport;

static STATUS_MUST_USE Status host_zig_names_list_type(Run *r, Pos pos) {
    return run_failf(r, pos, CLASS_TYPE, "`zig_move`'s names are a list of names, or `{{kind = ..., name = ...}}`", S("`zig_move`'s names are a list of names, or `{kind = ..., name = ...}`"));
}

static STATUS_MUST_USE Status host_zig_name_type(Run *r, Pos pos) {
    return run_failf(r, pos, CLASS_TYPE, "`zig_move`: a name is a string, or `{{kind = ..., name = ...}}`", S("`zig_move`: a name is a string, or `{kind = ..., name = ...}`"));
}

// `zig_move`'s names, Zig's loop over `names.list.items`.
static STATUS_MUST_USE Status host_zig_wants(Run *r, Pos pos, Table *names, HostZigWants *wants) {
    static const Str kinds_all[] = {S_ITEM("fn"), S_ITEM("const"), S_ITEM("var"), S_ITEM("test"), S_ITEM("comptime"), S_ITEM("field")};
    SLICE_TYPE(HostZigKinds, const Str);
    HostZigKinds kinds = {kinds_all, sizeof kinds_all / sizeof *kinds_all};
    for (size_t i = 0; i < names->list.len; i++) {
        Value v = at(names->list, i);
        HostZigWant w = {S(""), {NULL, 0}};
        if (v.tag == VALUE_STRING) {
            w.name = v.as.string;
        } else if (v.tag == VALUE_TABLE) {
            Table *t = v.as.table;
            if (t->kind != TABLE_MAP) return host_zig_name_type(r, pos);
            for (size_t k = 0; k < ValueMap_len(&t->map); k++) {
                Str key = ValueMap_key_at(&t->map, k);
                Value x = ValueMap_value_at(&t->map, k);
                if (x.tag != VALUE_STRING) return run_failf(r, pos, CLASS_TYPE, "`zig_move`: a name's `{s}` is a string", S("`zig_move`: a name's `"), key, S("` is a string"));
                Str s = x.as.string;
                if (str_eq(key, S("name"))) {
                    w.name = s;
                } else if (str_eq(key, S("kind"))) {
                    for (size_t j = 0; j < kinds.len; j++) {
                        if (str_eq(at(kinds, j), s)) w.kind = at(kinds, j);
                    }
                    if (w.kind.ptr == NULL) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_move`: no kind `{s}`: fn, const, var, test, comptime or field", S("`zig_move`: no kind `"), s, S("`: fn, const, var, test, comptime or field"));
                } else {
                    return run_failf(r, pos, CLASS_ARGUMENT, "`zig_move`: a name takes `kind` and `name`, not `{s}`", S("`zig_move`: a name takes `kind` and `name`, not `"), key, S("`"));
                }
            }
        } else {
            return host_zig_name_type(r, pos);
        }
        if (HostZigWants_push(wants, w) != OK) return run_values_oom(r, pos);
    }
    return OK;
}

// A move's options: `comment`, `after`, `from_as`.
typedef struct {
    Str mode;
    Str after;
    Str from_as;
} HostZigMoveOpts;

static STATUS_MUST_USE Status host_zig_move_opts(Run *r, Pos pos, const Value *v, HostZigMoveOpts *o) {
    o->mode = S("doc");
    o->after.ptr = NULL;
    o->after.len = 0;
    o->from_as = o->after;
    if (v->tag == VALUE_NIL) return OK;
    if (v->tag != VALUE_TABLE) return run_failf(r, pos, CLASS_TYPE, "`zig_move`'s options are a map, not {s}", S("`zig_move`'s options are a map, not "), value_type_name(v));
    Table *t = v->as.table;
    for (size_t k = 0; k < ValueMap_len(&t->map); k++) {
        Str key = ValueMap_key_at(&t->map, k);
        Value x = ValueMap_value_at(&t->map, k);
        if (x.tag != VALUE_STRING) return run_failf(r, pos, CLASS_TYPE, "`zig_move`'s `{s}` is a string", S("`zig_move`'s `"), key, S("` is a string"));
        Str s = x.as.string;
        if (str_eq(key, S("comment"))) {
            if (!str_eq(s, S("doc")) && !str_eq(s, S("doc_and_plain"))) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_move`'s `comment` is \"doc\" or \"doc_and_plain\", not \"{s}\"", S("`zig_move`'s `comment` is \"doc\" or \"doc_and_plain\", not \""), s, S("\""));
            o->mode = s;
        } else if (str_eq(key, S("after"))) {
            o->after = s;
        } else if (str_eq(key, S("from_as"))) {
            if (!zig_wire_valid_id(s)) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_move`'s `from_as` (\"{s}\") is not a Zig identifier", S("`zig_move`'s `from_as` (\""), s, S("\") is not a Zig identifier"));
            o->from_as = s;
        } else {
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_move` takes `comment`, `after` and `from_as`, not `{s}`", S("`zig_move` takes `comment`, `after` and `from_as`, not `"), key, S("`"));
        }
    }
    return OK;
}

// The part of `p` after its last `/`, Zig's `std.fs.path.basename` of a
// script's path.
static Str host_zig_basename(Str p) {
    size_t n = p.len;
    while (n > 1 && at(p, n - 1) == '/') n--;
    size_t i = n;
    while (i > 0 && at(p, i - 1) != '/') i--;
    return str_sub(p, i, n);
}

// `host("zig_move", from, to, {names, opts})`: moves top-level declarations
// from one Zig file to another (zig-ast plans it) and says what the move
// broke. A missing `to` is made. `to` is written before `from`, so a stop
// between them leaves the declarations in both, never in neither.
static STATUS_MUST_USE Status host_zig_move_in(Paths *h, HostZigProc *z, Arena *sa, Run *r, Pos pos, ConstValues args, Value *out) {
    r->tried_change = true;
    if (args.len != 3) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_move` takes `from`, `to` and `{{names, opts}}`, given {d}", S("`zig_move` takes `from`, `to` and `{names, opts}`, given "), run_unum(r, args.len));
    CheckedPath p;
    CheckedPath q;
    TRY(builtin_checked_path(r, pos, BUILTIN_HOST, &at(args, 0), false, &p));
    TRY(builtin_checked_path(r, pos, BUILTIN_HOST, &at(args, 1), false, &q));
    if (str_eq(p.full.s, q.full.s)) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`zig_move`: `from` and `to` are one file"));
    const Value *pv = &at(args, 2);
    if (pv->tag != VALUE_TABLE || pv->as.table->kind != TABLE_MAP) return run_fail(&r->ctx, pos, CLASS_TYPE, S("`zig_move`'s names are a list"));
    Table *packed_args = pv->as.table;
    Value names_v;
    if (!ValueMap_get(&packed_args->map, S("names"), &names_v)) value_nil(&names_v);
    if (names_v.tag != VALUE_TABLE || names_v.as.table->kind != TABLE_LIST) return host_zig_names_list_type(r, pos);
    HostZigWants wants = HostZigWants_make(r->ctx.values);
    TRY(host_zig_wants(r, pos, names_v.as.table, &wants));
    if (wants.len == 0) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`zig_move`: no names: nothing to move"));
    Value opts_v;
    if (!ValueMap_get(&packed_args->map, S("opts"), &opts_v)) value_nil(&opts_v);
    HostZigMoveOpts o;
    TRY(host_zig_move_opts(r, pos, &opts_v, &o));
    bool reach = o.from_as.ptr != NULL;
    Str from_src = {0};
    TRY(host_zig_read(r, pos, &p, &from_src));
    // `to` may be missing: then it is "", its read counted all the same.
    TRY(builtin_count_read(r, pos));
    bool found = false;
    FileSystemStat st;
    TRY(builtin_stat_or_null(r, pos, BUILTIN_HOST, &q, &found, &st));
    Str to_src = S("");
    if (found) {
        uint64_t size = 0;
        TRY(builtin_file_size(r, pos, BUILTIN_HOST, &q, &size));
        TRY(builtin_read_sized(r, pos, BUILTIN_HOST, &q, size, 0, BUILTIN_TO_END, &to_src));
        TRY(builtin_charge_bytes(r, pos, to_src.len));
    }
    Buf req = buf_make(sa);
    TRY(host_zig_req_line(r, pos, &req, S("move"), HOST_ZIG_NONE, HOST_ZIG_NONE));
    for (size_t k = 0; k < wants.len; k++) TRY(host_zig_req_line(r, pos, &req, S("want"), at(wants, k).name, at(wants, k).kind));
    TRY(host_zig_req_line(r, pos, &req, S("comment"), o.mode, HOST_ZIG_NONE));
    if (o.after.ptr != NULL) TRY(host_zig_req_line(r, pos, &req, S("after"), o.after, HOST_ZIG_NONE));
    if (reach) TRY(host_zig_req_line(r, pos, &req, S("from_as"), o.from_as, host_zig_basename(p.rel)));
    TRY(host_zig_req_ok(r, pos, zig_wire_source(&req, from_src)));
    TRY(host_zig_req_ok(r, pos, zig_wire_source(&req, to_src)));
    HostZigAnswer an;
    TRY(host_zig_ask(z, h, sa, r, pos, r->host_call, buf_view(&req), &an));
    static const Str group_names_all[] = {S_ITEM("to"), S_ITEM("from"), S_ITEM("to_q"), S_ITEM("to_i"), S_ITEM("from_p")};
    static const Str report_names_all[] = {S_ITEM("needs_pub"), S_ITEM("needs_import"), S_ITEM("needs_from_from"), S_ITEM("uses_left")};
    _Static_assert(sizeof group_names_all / sizeof *group_names_all == HOST_ZIG_GROUPS, "a name for each group");
    _Static_assert(sizeof report_names_all / sizeof *report_names_all == HOST_ZIG_REPORTS, "a name for each report");
    SLICE_TYPE(HostZigNames, const Str);
    HostZigNames group_names = {group_names_all, HOST_ZIG_GROUPS};
    HostZigNames report_names = {report_names_all, HOST_ZIG_REPORTS};
    HostZigEdits groups_room[HOST_ZIG_GROUPS];
    HostZigNameLines reports_room[HOST_ZIG_REPORTS];
    SLICE_TYPE(HostZigGroups, HostZigEdits);
    SLICE_TYPE(HostZigReports, HostZigNameLines);
    HostZigGroups groups = {groups_room, HOST_ZIG_GROUPS};
    HostZigReports reports = {reports_room, HOST_ZIG_REPORTS};
    for (size_t g = 0; g < groups.len; g++) at(groups, g) = HostZigEdits_make(sa);
    for (size_t g = 0; g < reports.len; g++) at(reports, g) = HostZigNameLines_make(sa);
    size_t group = SIZE_MAX;
    size_t moved = 0;
    size_t qualified = 0;
    HostZigStrs made_pub = HostZigStrs_make(sa);
    for (;;) {
        bool got = false;
        TRY(host_zig_next(&an, &got));
        if (!got) break;
        size_t n = 0;
        Str f = {0};
        size_t report = SIZE_MAX;
        for (size_t k = 0; k < report_names.len; k++) {
            if (host_zig_is(&an, at(report_names, k))) report = k;
        }
        if (host_zig_is(&an, S("work"))) {
            TRY(host_zig_work(&an));
        } else if (host_zig_is(&an, S("refused"))) {
            TRY(host_zig_field(&an, 1, &f));
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_move` {s} to {s}: {s}", S("`zig_move` "), p.rel, S(" to "), q.rel, S(": "), f);
        } else if (host_zig_is(&an, S("may_change"))) {
            // Both files are asked before either is written.
            TRY(change_may(r, pos, &q));
            TRY(change_may(r, pos, &p));
        } else if (host_zig_is(&an, S("moved_untokenized"))) {
            TRY(host_zig_int(&an, 1, &n));
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_move`: the moved code does not tokenize at line {d}", S("`zig_move`: the moved code does not tokenize at line "), run_unum(r, n));
        } else if (host_zig_is(&an, S("shadowed"))) {
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_move`: `{s}` is a name in the moved code already: `from_as` would shadow it; nothing written", S("`zig_move`: `"), o.from_as, S("` is a name in the moved code already: `from_as` would shadow it; nothing written"));
        } else if (host_zig_is(&an, S("moved_declares"))) {
            TRY(host_zig_field(&an, 1, &f));
            TRY(host_zig_int(&an, 2, &n));
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_move`: the moved code declares `{s}` (line {d}), which `from_as` would qualify; nothing written", S("`zig_move`: the moved code declares `"), f, S("` (line "), run_unum(r, n), S("), which `from_as` would qualify; nothing written"));
        } else if (host_zig_is(&an, S("moved_unparsed"))) {
            TRY(host_zig_int(&an, 1, &n));
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_move`: the moved code does not parse at line {d}", S("`zig_move`: the moved code does not parse at line "), run_unum(r, n));
        } else if (host_zig_is(&an, S("from_unparsed"))) {
            TRY(host_zig_int(&an, 1, &n));
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_move`: `from`, cut, does not parse at line {d}", S("`zig_move`: `from`, cut, does not parse at line "), run_unum(r, n));
        } else if (host_zig_is(&an, S("g"))) {
            TRY(host_zig_field(&an, 1, &f));
            group = SIZE_MAX;
            for (size_t g = 0; g < group_names.len; g++) {
                if (str_eq(at(group_names, g), f)) group = g;
            }
            if (group == SIZE_MAX) return host_zig_wrong(&an);
        } else if (host_zig_is(&an, S("s"))) {
            if (group == SIZE_MAX) return host_zig_wrong(&an);
            HostZigEdit e;
            TRY(host_zig_edit_of(&an, &e));
            if (HostZigEdits_push(&at(groups, group), e) != OK) return run_values_oom(r, pos);
        } else if (host_zig_is(&an, S("moved"))) {
            TRY(host_zig_int(&an, 1, &moved));
        } else if (host_zig_is(&an, S("qualified"))) {
            TRY(host_zig_int(&an, 1, &qualified));
        } else if (host_zig_is(&an, S("made_pub"))) {
            TRY(host_zig_field(&an, 1, &f));
            if (HostZigStrs_push(&made_pub, f) != OK) return run_values_oom(r, pos);
        } else if (report != SIZE_MAX) {
            bool targets = report == HOST_ZIG_NEEDS_IMPORT;
            HostZigNameLine x = {{0}, S(""), 0};
            TRY(host_zig_field(&an, 1, &x.name));
            if (targets) TRY(host_zig_field(&an, 2, &x.target));
            TRY(host_zig_int(&an, targets ? 3 : 2, &x.line));
            if (HostZigNameLines_push(&at(reports, report), x) != OK) return run_values_oom(r, pos);
        } else {
            return host_zig_wrong(&an);
        }
    }
    Str to_now = {0};
    Str from_now = {0};
    SpansItems to_spans;
    SpansItems from_spans;
    TRY(host_zig_apply(&an, to_src, HostZigEdits_items(&at(groups, HOST_ZIG_TO)), &to_now, &to_spans));
    TRY(host_zig_apply(&an, from_src, HostZigEdits_items(&at(groups, HOST_ZIG_FROM)), &from_now, &from_spans));
    // `from_as` rewrites, each against the text the one before it left.
    Str q_now = {0};
    Str i_now = {0};
    Str p_now = {0};
    SpansItems q_spans = {NULL, 0};
    SpansItems i_spans = {NULL, 0};
    SpansItems p_spans = {NULL, 0};
    if (reach) {
        TRY(host_zig_apply(&an, to_now, HostZigEdits_items(&at(groups, HOST_ZIG_TO_Q)), &q_now, &q_spans));
        TRY(host_zig_apply(&an, q_now, HostZigEdits_items(&at(groups, HOST_ZIG_TO_I)), &i_now, &i_spans));
        TRY(host_zig_apply(&an, from_now, HostZigEdits_items(&at(groups, HOST_ZIG_FROM_P)), &p_now, &p_spans));
    }
    // The budgets for every write, checked up front, so a budget never
    // stops a move halfway.
    uint64_t writes = reach ? 5 : 2;
    uint64_t bytes = to_now.len + from_now.len;
    if (reach) bytes += q_now.len + i_now.len + p_now.len;
    if (r->changes + writes > r->ctx.budgets.changes) return run_budget_stop(&r->ctx, pos, BUDGET_CHANGES);
    if (bytes > run_sat_sub(r->ctx.budgets.change_bytes, r->change_bytes)) return run_budget_stop(&r->ctx, pos, BUDGET_CHANGE_BYTES);
    TRY(host_zig_rewrite(r, pos, &q, to_src, to_now, to_spans));
    // The file system or the output budget can still stop the second write; the
    // message then says `to` already holds the copies.
    if (host_zig_rewrite(r, pos, &p, from_src, from_now, from_spans) != OK) {
        Stop *s = &r->ctx.stop;
        Msg m;
        msg_init(&m, r->ctx.values);
        msg_str(&m, s->message);
        msg_str(&m, S("; `zig_move` wrote "));
        msg_str(&m, q.rel);
        msg_str(&m, S(" first, so it holds the moved declarations, which "));
        msg_str(&m, p.rel);
        msg_str(&m, S(" still has: take "));
        msg_str(&m, q.rel);
        msg_str(&m, S(" back (`git checkout --`) before moving again"));
        if (m.s == OK) s->message = buf_view(&m.b);
        return STOP;
    }
    if (reach) {
        TRY(host_zig_rewrite(r, pos, &q, to_now, q_now, q_spans));
        TRY(host_zig_rewrite(r, pos, &q, q_now, i_now, i_spans));
        TRY(host_zig_rewrite(r, pos, &p, from_now, p_now, p_spans));
    }
    Table *t = NULL;
    TRY(host_zig_map(r, pos, &t));
    TRY(run_map_put_int(r, pos, t, S("moved"), (int64_t)moved));
    if (reach) {
        TRY(run_map_put_int(r, pos, t, S("qualified"), (int64_t)qualified));
        Table *mp = NULL;
        TRY(host_zig_list(r, pos, &mp));
        for (size_t k = 0; k < made_pub.len; k++) {
            Str kept = {0};
            TRY(host_zig_keep(r, pos, at(made_pub, k), &kept));
            TRY(builtin_push_str(r, pos, mp, kept));
        }
        TRY(host_zig_put_table(r, pos, t, S("made_pub"), mp));
    }
    for (size_t k = 0; k < reports.len; k++) TRY(host_zig_name_lines(r, pos, t, at(report_names, k), HostZigNameLines_items(&at(reports, k)), k == HOST_ZIG_NEEDS_IMPORT));
    value_table(out, t);
    return OK;
}

// ── zig_imports ──

// The folder of a path, Zig's `std.fs.path.dirnamePosix(p) orelse
// "."`.
static Str host_zig_dirname(Str p) {
    size_t n = p.len;
    while (n > 1 && at(p, n - 1) == '/') n--;
    if (n == 1 && at(p, 0) == '/') return S(".");
    size_t i = n;
    while (i > 0 && at(p, i - 1) != '/') i--;
    if (i == 0) return S(".");
    while (i > 1 && at(p, i - 1) == '/') i--;
    return str_sub(p, 0, i);
}

// Whether a target names a file, Zig's `std.fs.path.extension(t).len > 0`:
// a `.` in its last part, not first.
static bool host_zig_has_extension(Str t) {
    Str base = host_zig_basename(t);
    size_t i = base.len;
    while (i > 0 && at(base, i - 1) != '.') i--;
    return i > 1;
}

// Zig's `std.fs.path.resolvePosix(dir, target)` of paths that may be
// relative: `.` and empty parts dropped, `..` taking one part back, kept
// where none is left to take; "." for nothing.
static bool host_zig_resolve(Arena *a, Str dir, Str target, Str *out) {
    bool absolute = (dir.len > 0 && at(dir, 0) == '/') || (target.len > 0 && at(target, 0) == '/');
    HostZigStrs parts = HostZigStrs_make(a);
    for (int k = 0; k < 2; k++) {
        Str s = k == 0 ? dir : target;
        if (k == 1 && s.len > 0 && at(s, 0) == '/') parts.len = 0;
        size_t from = 0;
        for (size_t i = 0; i <= s.len; i++) {
            if (i < s.len && at(s, i) != '/') continue;
            Str part = str_sub(s, from, i);
            from = i + 1;
            if (part.len == 0 || str_eq(part, S("."))) continue;
            if (str_eq(part, S(".."))) {
                if (parts.len > 0 && !str_eq(at(parts, parts.len - 1), S(".."))) {
                    parts.len--;
                    continue;
                }
                if (absolute) continue;
            }
            if (HostZigStrs_push(&parts, part) != OK) return false;
        }
    }
    Buf b = buf_make(a);
    if (absolute && buf_byte(&b, '/') != OK) return false;
    for (size_t k = 0; k < parts.len; k++) {
        if (k > 0 && buf_byte(&b, '/') != OK) return false;
        if (buf_str(&b, at(parts, k)) != OK) return false;
    }
    if (b.len == 0 && buf_byte(&b, '.') != OK) return false;
    *out = buf_view(&b);
    return true;
}

// `host("zig_imports", path)`: a Zig file's `@import`s and `@embedFile`s in
// source order, as `{kind, target, name, line, file}`, `file` being `found`
// or `missing` beside the importer, or `module` for a target with no
// extension.
static STATUS_MUST_USE Status host_zig_imports_in(Paths *h, HostZigProc *z, Arena *sa, Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len != 1) return run_failf(r, pos, CLASS_ARGUMENT, "`zig_imports` takes a path, given {d} arguments", S("`zig_imports` takes a path, given "), run_unum(r, args.len), S(" arguments"));
    CheckedPath p;
    TRY(builtin_checked_path(r, pos, BUILTIN_HOST, &at(args, 0), false, &p));
    Str src = {0};
    TRY(host_zig_read(r, pos, &p, &src));
    Buf req = buf_make(sa);
    TRY(host_zig_req_line(r, pos, &req, S("imports"), HOST_ZIG_NONE, HOST_ZIG_NONE));
    TRY(host_zig_req_ok(r, pos, zig_wire_source(&req, src)));
    HostZigAnswer an;
    TRY(host_zig_ask(z, h, sa, r, pos, r->host_call, buf_view(&req), &an));
    Str dir = host_zig_dirname(p.full.s);
    Table *list = NULL;
    TRY(host_zig_list(r, pos, &list));
    RunFiles *rs = builtin_files_of(r);
    for (;;) {
        bool got = false;
        TRY(host_zig_next(&an, &got));
        if (!got) break;
        size_t n = 0;
        if (host_zig_is(&an, S("work"))) {
            TRY(host_zig_work(&an));
        } else if (host_zig_is(&an, S("i"))) {
            Str target = {0};
            TRY(host_zig_field(&an, 2, &target));
            Str file = S("module");
            if (host_zig_has_extension(target)) {
                Str full = {0};
                if (!host_zig_resolve(sa, dir, target, &full)) return run_values_oom(r, pos);
                // The target is the file's own text, which may hold a NUL.
                OsStr full_os;
                if (!os_str_of(full, &full_os)) return run_failf(r, pos, CLASS_REFUSED, "`zig_imports`: an import of {s} holds a NUL byte, which no path can carry", S("`zig_imports`: an import of "), p.rel, S(" holds a NUL byte, which no path can carry"));
                bool found = false;
                FileSystemStat st;
                FileSystemErr e = file_system_stat_or_null(rs->file_system, full_os, &found, &st);
                if (e != FILE_SYSTEM_OK) {
                    Str kept = {0};
                    TRY(host_zig_keep(r, pos, target, &kept));
                    return builtin_file_system_failed(r, pos, BUILTIN_HOST, kept, e);
                }
                file = found ? S("found") : S("missing");
            }
            Table *m = NULL;
            TRY(host_zig_map(r, pos, &m));
            Str f = {0};
            TRY(host_zig_field(&an, 1, &f));
            TRY(host_zig_put_kept(r, pos, m, S("kind"), f));
            TRY(host_zig_put_kept(r, pos, m, S("target"), target));
            TRY(host_zig_field(&an, 3, &f));
            TRY(host_zig_put_kept(r, pos, m, S("name"), f));
            TRY(host_zig_int(&an, 4, &n));
            TRY(run_map_put_int(r, pos, m, S("line"), (int64_t)n));
            TRY(run_map_put_str(r, pos, m, S("file"), file));
            TRY(host_zig_push_table(r, pos, list, m));
        } else if (host_zig_is(&an, S("parse_error"))) {
            TRY(host_zig_int(&an, 1, &n));
            return run_failf(r, pos, CLASS_ARGUMENT, "`zig_imports`: {s} does not parse as Zig: line {d}", S("`zig_imports`: "), p.rel, S(" does not parse as Zig: line "), run_unum(r, n));
        } else {
            return host_zig_wrong(&an);
        }
    }
    value_table(out, list);
    return OK;
}

// ── the calls ──

typedef Status (*HostZigCall)(Paths *h, HostZigProc *z, Arena *sa, Run *r, Pos pos, ConstValues args, Value *out);

// One of the Zig calls, with its own scratch for the request and the answer:
// an arena capped by what the run's arena budget has left, as Zig's
// `Scratch` is, and given back when the call returns.
static STATUS_MUST_USE Status host_zig_with_scratch(HostZigCall f, Paths *h, HostZigProc *z, Run *r, Pos pos, ConstValues args, Value *out) {
    Arena sa;
    arena_init_within(&sa, r->ctx.values);
    Status s = f(h, z, &sa, r, pos, args, out);
    arena_deinit(&sa);
    return s;
}

// The Zig call `name`, `*handled` false for a name that is none of them,
// asked of the run's zig-ast `z`.
static STATUS_MUST_USE Status host_zig_call(Paths *h, HostZigProc *z, Run *r, Pos pos, Str name, ConstValues args, Value *out, bool *handled) {
    *handled = true;
    if (str_eq(name, S("zig_rename"))) return host_zig_with_scratch(host_zig_rename_in, h, z, r, pos, args, out);
    if (str_eq(name, S("zig_decls"))) return host_zig_with_scratch(host_zig_decls_in, h, z, r, pos, args, out);
    if (str_eq(name, S("zig_move"))) return host_zig_with_scratch(host_zig_move_in, h, z, r, pos, args, out);
    if (str_eq(name, S("zig_imports"))) return host_zig_with_scratch(host_zig_imports_in, h, z, r, pos, args, out);
    if (str_eq(name, S("zig_tokens"))) return host_zig_with_scratch(host_zig_tokens_in, h, z, r, pos, args, out);
    *handled = false;
    return OK;
}
