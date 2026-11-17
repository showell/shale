// host_text.c: the Linux host's two calls over text: `host("re_lines", s,
// pattern, opts?)`, the lines a regular expression matches, searched by the
// regex builtins' own engine, cache and charges; and `host("shale_tokens",
// path, opts?)`, a Shale file's tokens by the lexer the language runs on.
//
// Include run.h, builtins.c, builtins_text.c and builtins_store.c first.

static STATUS_MUST_USE Status re_lines_text(Run *r, Pos pos, const Value *v, Str what, Str *out) {
    if (v->tag == VALUE_STRING) {
        *out = v->as.string;
        return OK;
    }
    return run_failf(r, pos, CLASS_TYPE, "`re_lines`: {s} must be a string, not {s}", S("`re_lines`: "), what, S(" must be a string, not "), value_type_name(v));
}

// `re_lines`' options: every name checked first, then `nocase`, then
// `invert`, so a stop names the first wrong one in that order.
static STATUS_MUST_USE Status re_lines_options(Run *r, Pos pos, const Value *opts, RegexOptions *o, bool *invert) {
    if (opts->tag != VALUE_TABLE) return run_failf(r, pos, CLASS_TYPE, "`re_lines` takes its options as a map, not {s}", S("`re_lines` takes its options as a map, not "), value_type_name(opts));
    Table *m = opts->as.table;
    if (m->kind == TABLE_LIST) return run_fail(&r->ctx, pos, CLASS_TYPE, S("`re_lines` takes its options as a map, given a list"));
    for (size_t k = 0; k < ValueMap_len(&m->map); k++) {
        Str key = ValueMap_key_at(&m->map, k);
        if (!str_eq(key, S("nocase")) && !str_eq(key, S("invert"))) return run_failf(r, pos, CLASS_FAIL, "`re_lines` takes `invert` and `nocase`, not `{s}`", S("`re_lines` takes `invert` and `nocase`, not `"), key, S("`"));
    }
    Value x;
    if (ValueMap_get(&m->map, S("nocase"), &x)) TRY(builtin_flag_option(r, pos, S("re_lines"), S("nocase"), &x, &o->nocase));
    if (ValueMap_get(&m->map, S("invert"), &x)) TRY(builtin_flag_option(r, pos, S("re_lines"), S("invert"), &x, invert));
    return OK;
}

// `host("re_lines", s, pattern[, {invert, nocase}])`: the lines of `s`, split
// as `lines` splits them, that `pattern` matches (`invert`: the others), each
// `{line, text}` numbered from 1; none is `{}`. Each line is searched alone,
// so `^`, `$` and `\b` see its edges. Charged a step for each 16 bytes of `s`,
// a step a line, a step a line kept, and each line's search.
static STATUS_MUST_USE Status host_re_lines(Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len < 2 || args.len > 3) return run_failf(r, pos, CLASS_ARGUMENT, "`re_lines` takes a text, a pattern and options, given {d} arguments", S("`re_lines` takes a text, a pattern and options, given "), run_unum(r, args.len), S(" arguments"));
    Str s = S("");
    Str pattern = S("");
    TRY(re_lines_text(r, pos, &at(args, 0), S("its first argument"), &s));
    TRY(re_lines_text(r, pos, &at(args, 1), S("its pattern"), &pattern));
    RegexOptions options;
    memset(&options, 0, sizeof options);
    bool invert = false;
    if (args.len == 3) TRY(re_lines_options(r, pos, &at(args, 2), &options, &invert));
    RegexCached *c = NULL;
    TRY(builtin_program(r, pos, S("re_lines"), pattern, options, &c));
    TRY(builtin_charge_bytes(r, pos, s.len));
    Table *list = NULL;
    TRY(run_new_table(r, pos, &list));
    Searches each = {0, 0};
    int64_t n = 0;
    Str rest = s;
    while (rest.len > 0) {
        const char *nl = str_find_byte(rest, '\n');
        size_t end = nl == NULL ? rest.len : (size_t)(nl - rest.ptr);
        Str line = str_sub(rest, 0, end);
        rest = nl == NULL ? S("") : str_sub(rest, end + 1, rest.len);
        n++;
        TRY(run_charge(r, pos, 1));
        bool found = false;
        RegexFound match;
        TRY(searches_from(&each, r, pos, c, line, 0, &found, &match));
        if (found == invert) continue;
        TRY(run_charge(r, pos, 1));
        Table *m = NULL;
        TRY(run_new_table(r, pos, &m));
        m->kind = TABLE_MAP;
        TRY(run_map_put_int(r, pos, m, S("line"), n));
        TRY(run_map_put_str(r, pos, m, S("text"), line));
        // A list once it holds a line, `{}` as the prelude's was while it
        // holds none.
        list->kind = TABLE_LIST;
        Value mv;
        value_table(&mv, m);
        TRY(run_list_push(r, pos, list, &mv));
    }
    TRY(searches_done(&each, r, pos));
    value_table(out, list);
    return OK;
}

// Zig's `lex.Tag` names, in the enum's order: what `shale_tokens` gives as a
// token's `tag` and takes in `{tag = ...}`.
static Names shale_token_names(void) {
    static const Str names[] = {
        S_ITEM("eof"), S_ITEM("name"), S_ITEM("int"), S_ITEM("string"), S_ITEM("comment"),
        S_ITEM("kw_and"), S_ITEM("kw_break"), S_ITEM("kw_const"), S_ITEM("kw_continue"),
        S_ITEM("kw_do"), S_ITEM("kw_else"), S_ITEM("kw_elseif"), S_ITEM("kw_end"),
        S_ITEM("kw_false"), S_ITEM("kw_for"), S_ITEM("kw_function"), S_ITEM("kw_if"),
        S_ITEM("kw_in"), S_ITEM("kw_local"), S_ITEM("kw_nil"), S_ITEM("kw_not"), S_ITEM("kw_or"),
        S_ITEM("kw_return"), S_ITEM("kw_then"), S_ITEM("kw_true"), S_ITEM("kw_while"),
        S_ITEM("plus"), S_ITEM("minus"), S_ITEM("star"), S_ITEM("slash_slash"), S_ITEM("percent"),
        S_ITEM("dot_dot"), S_ITEM("eq_eq"), S_ITEM("not_eq"), S_ITEM("lt"), S_ITEM("le"),
        S_ITEM("gt"), S_ITEM("ge"), S_ITEM("assign"), S_ITEM("lparen"), S_ITEM("rparen"),
        S_ITEM("lbrace"), S_ITEM("rbrace"), S_ITEM("lbracket"), S_ITEM("rbracket"),
        S_ITEM("comma"), S_ITEM("dot"), S_ITEM("hash"), S_ITEM("question"), S_ITEM("semicolon"),
    };
    _Static_assert(sizeof names / sizeof *names == TAG_SEMICOLON + 1, "a name for every token tag");
    Names n = {names, sizeof names / sizeof *names};
    return n;
}

// The tag `{tag = name}` keeps, Zig's `tagText`, then its `stringToEnum`:
// `*has` false when the options name none.
static STATUS_MUST_USE Status shale_tokens_tag(Run *r, Pos pos, const Value *opts, bool *has, Tag *out) {
    *has = false;
    if (opts->tag != VALUE_TABLE) return run_failf(r, pos, CLASS_TYPE, "`shale_tokens`' options are a map, not {s}", S("`shale_tokens`' options are a map, not "), value_type_name(opts));
    Table *t = opts->as.table;
    // An empty table is no map to Zig's check, so `{}` is said as a list.
    if (t->kind != TABLE_MAP) return run_fail(&r->ctx, pos, CLASS_TYPE, S("`shale_tokens`' options are a map, not a list"));
    Str only = S("");
    for (size_t k = 0; k < ValueMap_len(&t->map); k++) {
        Str key = ValueMap_key_at(&t->map, k);
        Value x = ValueMap_value_at(&t->map, k);
        if (!str_eq(key, S("tag"))) return run_failf(r, pos, CLASS_ARGUMENT, "`shale_tokens` takes `tag`, not `{s}`", S("`shale_tokens` takes `tag`, not `"), key, S("`"));
        if (x.tag != VALUE_STRING) return run_failf(r, pos, CLASS_TYPE, "`shale_tokens`' `tag` is a tag's name, not {s}", S("`shale_tokens`' `tag` is a tag's name, not "), value_type_name(&x));
        only = x.as.string;
        *has = true;
    }
    if (!*has) return OK;
    Names names = shale_token_names();
    for (size_t k = 0; k < names.len; k++) {
        if (str_eq(at(names, k), only)) {
            *out = (Tag)k;
            return OK;
        }
    }
    return run_failf(r, pos, CLASS_ARGUMENT, "`shale_tokens`: no token tag `{s}` (`name`, `int`, `string`, `comment`, `kw_end`, `plus`, ...)",
                     S("`shale_tokens`: no token tag `"), only, S("` (`name`, `int`, `string`, `comment`, `kw_end`, `plus`, ...)"));
}

// `host("shale_tokens", path[, {tag}])`: the file's tokens in order, comments
// among them, each `{tag, text, line, start, stop}`: `text` is the token's
// source bytes and `read(path, start, stop - start)` the token. A file that
// does not lex stops the run with the lexer's line and message. The read is
// charged as any read and its bytes once more, and a step a token.
static STATUS_MUST_USE Status host_shale_tokens(Run *r, Pos pos, ConstValues args, Value *out) {
    if (args.len < 1 || args.len > 2) return run_failf(r, pos, CLASS_ARGUMENT, "`shale_tokens` takes a path and options, given {d} arguments", S("`shale_tokens` takes a path and options, given "), run_unum(r, args.len), S(" arguments"));
    bool has_only = false;
    Tag only = TAG_EOF;
    if (args.len == 2) TRY(shale_tokens_tag(r, pos, &at(args, 1), &has_only, &only));
    if (has_only && only == TAG_EOF) return run_fail(&r->ctx, pos, CLASS_ARGUMENT, S("`shale_tokens`: no token tag `eof`: the end of a file is no token here"));
    StorePath p;
    TRY(builtin_path(r, pos, BUILTIN_HOST, &at(args, 0), false, &p));
    TRY(builtin_count_read(r, pos));
    uint64_t size = 0;
    TRY(builtin_file_size(r, pos, BUILTIN_HOST, &p, &size));
    Str src = S("");
    TRY(builtin_read_sized(r, pos, BUILTIN_HOST, &p, size, 0, false, 0, &src));
    TRY(builtin_charge_bytes(r, pos, src.len));
    Lexer lx;
    lexer_init(&lx, r->ctx.values, src);
    Table *list = NULL;
    TRY(run_new_table(r, pos, &list));
    list->kind = TABLE_LIST;
    Names names = shale_token_names();
    while (true) {
        Token tok;
        if (lexer_next_or_comment(&lx, &tok) != OK) {
            if (lx.oom) return run_values_oom(r, pos);
            return run_failf(r, pos, CLASS_ARGUMENT, "`shale_tokens`: {s} does not lex as Shale at line {d}: {s}",
                             S("`shale_tokens`: "), p.rel, S(" does not lex as Shale at line "), run_unum(r, lx.err.line), S(": "), lx.err.message);
        }
        if (tok.tag == TAG_EOF) break;
        TRY(run_charge(r, pos, 1));
        if (has_only && tok.tag != only) continue;
        Table *m = NULL;
        TRY(run_new_table(r, pos, &m));
        m->kind = TABLE_MAP;
        TRY(run_map_put_str(r, pos, m, S("tag"), at(names, (size_t)tok.tag)));
        TRY(run_map_put_str(r, pos, m, S("text"), str_sub(src, tok.start, tok.stop)));
        TRY(run_map_put_int(r, pos, m, S("line"), tok.line));
        TRY(run_map_put_int(r, pos, m, S("start"), (int64_t)tok.start));
        TRY(run_map_put_int(r, pos, m, S("stop"), (int64_t)tok.stop));
        Value mv;
        value_table(&mv, m);
        TRY(run_list_push(r, pos, list, &mv));
    }
    value_table(out, list);
    return OK;
}
