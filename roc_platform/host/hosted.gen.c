// hosted.gen.c: written by c/gen_builtins.shale from c/builtins.txt; never
// edit it. The hosted functions of Shale's Roc platform (roc_platform/
// README.md): for each one, its kind, the driver's Shale that answers it, the
// shim Roc calls, its request as a Shale map and its answer as Roc's value.
// host.c includes it after the helpers it names (`roc_ask`, `hosted_*`); the
// C types are roc glue's (roc_platform_abi.h).

typedef enum {
    HOSTED_ARG,
    HOSTED_EXISTS,
    HOSTED_FAIL,
    HOSTED_GLOB,
    HOSTED_GREP_ALL,
    HOSTED_LIST,
    HOSTED_PRINT,
    HOSTED_RE_FIND,
    HOSTED_RE_FIND_ALL,
    HOSTED_RE_LINES,
    HOSTED_RE_MATCH,
    HOSTED_RE_REPLACE,
    HOSTED_READ,
    HOSTED_ROC_TREE,
    HOSTED_RUN,
    HOSTED_WRITE,
    HOSTED_COUNT,
} Hosted;

// Each kind's `fn` in the request.
static const Str hosted_names[] = {
    S_ITEM("arg"),
    S_ITEM("exists"),
    S_ITEM("fail"),
    S_ITEM("glob"),
    S_ITEM("grep_all"),
    S_ITEM("list"),
    S_ITEM("print"),
    S_ITEM("re_find"),
    S_ITEM("re_find_all"),
    S_ITEM("re_lines"),
    S_ITEM("re_match"),
    S_ITEM("re_replace"),
    S_ITEM("read"),
    S_ITEM("roc_tree"),
    S_ITEM("run"),
    S_ITEM("write"),
};

// Each kind's Roc name, as a stop at the Roc's line names the call.
static const Str hosted_roc_names[] = {
    S_ITEM("Shale.arg!"),
    S_ITEM("Shale.exists!"),
    S_ITEM("Shale.fail!"),
    S_ITEM("Shale.glob!"),
    S_ITEM("Shale.grep_all!"),
    S_ITEM("Shale.list!"),
    S_ITEM("Shale.print!"),
    S_ITEM("Shale.re_find!"),
    S_ITEM("Shale.re_find_all!"),
    S_ITEM("Shale.re_lines!"),
    S_ITEM("Shale.re_match!"),
    S_ITEM("Shale.re_replace!"),
    S_ITEM("Shale.read!"),
    S_ITEM("Shale.roc_tree!"),
    S_ITEM("Shale.run!"),
    S_ITEM("Shale.write!"),
};

// The Shale that runs the app.
static const char HOSTED_DRIVER[] =
    "-- the Roc platform's driver, written by c/gen_builtins.shale from c/builtins.txt\n"
    "function hosted_arg(q)\n"
    "  return arg(q.name, q.fallback)\n"
    "end\n"
    "function hosted_exists(q)\n"
    "  return exists(q.path)\n"
    "end\n"
    "function hosted_fail(q)\n"
    "  fail(q.why)\n"
    "end\n"
    "function hosted_glob(q)\n"
    "  return glob(q.pattern)\n"
    "end\n"
    "function hosted_grep_all(q)\n"
    "  return grep_all(q.path, q.literal)\n"
    "end\n"
    "function hosted_list(q)\n"
    "  if not exists(q.path) then return {tag = \"Missing\"} end\n"
    "  return {tag = \"Found\", value = ls(q.path)}\n"
    "end\n"
    "function hosted_print(q)\n"
    "  print(q.text)\n"
    "  return {}\n"
    "end\n"
    "function hosted_re_find(q)\n"
    "  local m = re_find(q.text, q.pattern)\n"
    "  if m == nil then return {tag = \"NoMatch\"} end\n"
    "  return {tag = \"Found\", value = {at = m.at, text = m.text, groups = get(m, \"groups\", {})}}\n"
    "end\n"
    "function hosted_re_find_all(q)\n"
    "  local out = {}\n"
    "  for m in re_find_all(q.text, q.pattern) do push(out, {at = m.at, text = m.text, groups = get(m, \"groups\", {})}) end\n"
    "  return out\n"
    "end\n"
    "function hosted_re_lines(q)\n"
    "  return re_lines(q.text, q.pattern)\n"
    "end\n"
    "function hosted_re_match(q)\n"
    "  return re_match(q.text, q.pattern)\n"
    "end\n"
    "function hosted_re_replace(q)\n"
    "  return re_replace(q.text, q.pattern, q.replacement)\n"
    "end\n"
    "function hosted_read(q)\n"
    "  if not exists(q.path) then return {tag = \"Missing\"} end\n"
    "  return {tag = \"Found\", value = read(q.path)}\n"
    "end\n"
    "function hosted_roc_tree(q)\n"
    "  local out = {}\n"
    "  for f in roc_tree(q.paths) do\n"
    "    local nodes = {}\n"
    "    for n in f.nodes do\n"
    "      local attrs = {}\n"
    "      for k in keys(n) do\n"
    "        if not has_item({\"kind\", \"depth\", \"parent\", \"kids\", \"line\", \"col\", \"end_line\", \"end_col\"}, k) then push(attrs, {name = k, value = tostring(n[k])}) end\n"
    "      end\n"
    "      push(nodes, {kind = n.kind, depth = n.depth, parent = n.parent, kids = n.kids, line = n.line, col = n.col, end_line = n.end_line, end_col = n.end_col, attrs = attrs})\n"
    "    end\n"
    "    push(out, {path = f.path, err = f.err, nodes = nodes})\n"
    "  end\n"
    "  return out\n"
    "end\n"
    "function hosted_run(q)\n"
    "  local opts = {timeout_ms = q.spec.timeout_ms}\n"
    "  opts[lower(q.spec.keep.tag)] = q.spec.keep.value\n"
    "  if q.spec.want ~= \"\" then opts.want = q.spec.want end\n"
    "  local r = run(q.spec.argv, opts)\n"
    "  return {code = get(r, \"code\", -1), out = r.out}\n"
    "end\n"
    "function hosted_write(q)\n"
    "  replace(q.path, q.text)\n"
    "  return {}\n"
    "end\n"
    "function answer(q)\n"
    "  local f = q.fn\n"
    "  if f == \"arg\" then return hosted_arg(q) end\n"
    "  if f == \"exists\" then return hosted_exists(q) end\n"
    "  if f == \"fail\" then return hosted_fail(q) end\n"
    "  if f == \"glob\" then return hosted_glob(q) end\n"
    "  if f == \"grep_all\" then return hosted_grep_all(q) end\n"
    "  if f == \"list\" then return hosted_list(q) end\n"
    "  if f == \"print\" then return hosted_print(q) end\n"
    "  if f == \"re_find\" then return hosted_re_find(q) end\n"
    "  if f == \"re_find_all\" then return hosted_re_find_all(q) end\n"
    "  if f == \"re_lines\" then return hosted_re_lines(q) end\n"
    "  if f == \"re_match\" then return hosted_re_match(q) end\n"
    "  if f == \"re_replace\" then return hosted_re_replace(q) end\n"
    "  if f == \"read\" then return hosted_read(q) end\n"
    "  if f == \"roc_tree\" then return hosted_roc_tree(q) end\n"
    "  if f == \"run\" then return hosted_run(q) end\n"
    "  if f == \"write\" then return hosted_write(q) end\n"
    "  if f == \"crash\" then fail(\"the Roc crashed: \" .. q.why) end\n"
    "  fail(\"the Roc platform has no hosted function `\" .. f .. \"`\")\n"
    "end\n"
    "local q = host(\"roc_step\")\n"
    "while q.fn ~= \"done\" do\n"
    "  q = host(\"roc_step\", answer(q))\n"
    "end\n"
    ;

// What the Roc's last call passed, read on Shale's side while the Roc waits.
static struct {
    RocStr name;
    RocStr fallback;
} hosted_arg_args;
static struct {
    RocStr path;
} hosted_exists_args;
static struct {
    RocStr why;
} hosted_fail_args;
static struct {
    RocStr pattern;
} hosted_glob_args;
static struct {
    RocStr path;
    RocStr literal;
} hosted_grep_all_args;
static struct {
    RocStr path;
} hosted_list_args;
static struct {
    RocStr text;
} hosted_print_args;
static struct {
    RocStr text;
    RocStr pattern;
} hosted_re_find_args;
static struct {
    RocStr text;
    RocStr pattern;
} hosted_re_find_all_args;
static struct {
    RocStr text;
    RocStr pattern;
} hosted_re_lines_args;
static struct {
    RocStr text;
    RocStr pattern;
} hosted_re_match_args;
static struct {
    RocStr text;
    RocStr pattern;
    RocStr replacement;
} hosted_re_replace_args;
static struct {
    RocStr path;
} hosted_read_args;
static struct {
    RocList paths;
} hosted_roc_tree_args;
static struct {
    ShaleRunArgs spec;
} hosted_run_args;
static struct {
    RocStr path;
    RocStr text;
} hosted_write_args;

// The answer Shale gave the Roc's last call.
static union {
    uint8_t none;
    RocStr arg;
    bool exists;
    RocList glob;
    RocList grep_all;
    FoundOrMissingType7 list;
    FoundOrNoMatch re_find;
    RocList re_find_all;
    RocList re_lines;
    bool re_match;
    RocStr re_replace;
    FoundOrMissingType11 read;
    RocList roc_tree;
    AnonStruct1763995a077e9c1f run;
} hosted_answer;

// ── the shims: the Roc's side of each hosted function ──

RocStr shale_arg(RocStr arg0, RocStr arg1) {
    hosted_arg_args.name = arg0;
    hosted_arg_args.fallback = arg1;
    roc_ask(HOSTED_ARG, __builtin_return_address(0));
    return hosted_answer.arg;
}

bool shale_exists(RocStr arg0) {
    hosted_exists_args.path = arg0;
    roc_ask(HOSTED_EXISTS, __builtin_return_address(0));
    return hosted_answer.exists;
}

void shale_fail(RocStr arg0) {
    hosted_fail_args.why = arg0;
    roc_ask(HOSTED_FAIL, __builtin_return_address(0));
}

RocList shale_glob(RocStr arg0) {
    hosted_glob_args.pattern = arg0;
    roc_ask(HOSTED_GLOB, __builtin_return_address(0));
    return hosted_answer.glob;
}

RocList shale_grep_all(RocStr arg0, RocStr arg1) {
    hosted_grep_all_args.path = arg0;
    hosted_grep_all_args.literal = arg1;
    roc_ask(HOSTED_GREP_ALL, __builtin_return_address(0));
    return hosted_answer.grep_all;
}

FoundOrMissingType7 shale_list(RocStr arg0) {
    hosted_list_args.path = arg0;
    roc_ask(HOSTED_LIST, __builtin_return_address(0));
    return hosted_answer.list;
}

void shale_print(RocStr arg0) {
    hosted_print_args.text = arg0;
    roc_ask(HOSTED_PRINT, __builtin_return_address(0));
}

FoundOrNoMatch shale_re_find(RocStr arg0, RocStr arg1) {
    hosted_re_find_args.text = arg0;
    hosted_re_find_args.pattern = arg1;
    roc_ask(HOSTED_RE_FIND, __builtin_return_address(0));
    return hosted_answer.re_find;
}

RocList shale_re_find_all(RocStr arg0, RocStr arg1) {
    hosted_re_find_all_args.text = arg0;
    hosted_re_find_all_args.pattern = arg1;
    roc_ask(HOSTED_RE_FIND_ALL, __builtin_return_address(0));
    return hosted_answer.re_find_all;
}

RocList shale_re_lines(RocStr arg0, RocStr arg1) {
    hosted_re_lines_args.text = arg0;
    hosted_re_lines_args.pattern = arg1;
    roc_ask(HOSTED_RE_LINES, __builtin_return_address(0));
    return hosted_answer.re_lines;
}

bool shale_re_match(RocStr arg0, RocStr arg1) {
    hosted_re_match_args.text = arg0;
    hosted_re_match_args.pattern = arg1;
    roc_ask(HOSTED_RE_MATCH, __builtin_return_address(0));
    return hosted_answer.re_match;
}

RocStr shale_re_replace(RocStr arg0, RocStr arg1, RocStr arg2) {
    hosted_re_replace_args.text = arg0;
    hosted_re_replace_args.pattern = arg1;
    hosted_re_replace_args.replacement = arg2;
    roc_ask(HOSTED_RE_REPLACE, __builtin_return_address(0));
    return hosted_answer.re_replace;
}

FoundOrMissingType11 shale_read(RocStr arg0) {
    hosted_read_args.path = arg0;
    roc_ask(HOSTED_READ, __builtin_return_address(0));
    return hosted_answer.read;
}

RocList shale_roc_tree(RocList arg0) {
    hosted_roc_tree_args.paths = arg0;
    roc_ask(HOSTED_ROC_TREE, __builtin_return_address(0));
    return hosted_answer.roc_tree;
}

AnonStruct1763995a077e9c1f shale_run(ShaleRunArgs arg0) {
    hosted_run_args.spec = arg0;
    roc_ask(HOSTED_RUN, __builtin_return_address(0));
    return hosted_answer.run;
}

void shale_write(RocStr arg0, RocStr arg1) {
    hosted_write_args.path = arg0;
    hosted_write_args.text = arg1;
    roc_ask(HOSTED_WRITE, __builtin_return_address(0));
}

// ── Shale's side ──

// The Roc's request `kind` as the fields of the map `q`, each argument by
// its name in the table's `roc_params`.
static STATUS_MUST_USE Status hosted_request(Run *r, Pos pos, int kind, Table *q) {
    switch (kind) {
    case HOSTED_ARG: {
        Value name_1;
        TRY(hosted_put_str(r, pos, &(hosted_arg_args.name), &name_1));
        TRY(run_map_put(r, pos, q, S("name"), &name_1));
        Value fallback_2;
        TRY(hosted_put_str(r, pos, &(hosted_arg_args.fallback), &fallback_2));
        TRY(run_map_put(r, pos, q, S("fallback"), &fallback_2));
        return OK;
    }
    case HOSTED_EXISTS: {
        Value path_3;
        TRY(hosted_put_str(r, pos, &(hosted_exists_args.path), &path_3));
        TRY(run_map_put(r, pos, q, S("path"), &path_3));
        return OK;
    }
    case HOSTED_FAIL: {
        Value why_4;
        TRY(hosted_put_str(r, pos, &(hosted_fail_args.why), &why_4));
        TRY(run_map_put(r, pos, q, S("why"), &why_4));
        return OK;
    }
    case HOSTED_GLOB: {
        Value pattern_5;
        TRY(hosted_put_str(r, pos, &(hosted_glob_args.pattern), &pattern_5));
        TRY(run_map_put(r, pos, q, S("pattern"), &pattern_5));
        return OK;
    }
    case HOSTED_GREP_ALL: {
        Value path_6;
        TRY(hosted_put_str(r, pos, &(hosted_grep_all_args.path), &path_6));
        TRY(run_map_put(r, pos, q, S("path"), &path_6));
        Value literal_7;
        TRY(hosted_put_str(r, pos, &(hosted_grep_all_args.literal), &literal_7));
        TRY(run_map_put(r, pos, q, S("literal"), &literal_7));
        return OK;
    }
    case HOSTED_LIST: {
        Value path_8;
        TRY(hosted_put_str(r, pos, &(hosted_list_args.path), &path_8));
        TRY(run_map_put(r, pos, q, S("path"), &path_8));
        return OK;
    }
    case HOSTED_PRINT: {
        Value text_9;
        TRY(hosted_put_str(r, pos, &(hosted_print_args.text), &text_9));
        TRY(run_map_put(r, pos, q, S("text"), &text_9));
        return OK;
    }
    case HOSTED_RE_FIND: {
        Value text_10;
        TRY(hosted_put_str(r, pos, &(hosted_re_find_args.text), &text_10));
        TRY(run_map_put(r, pos, q, S("text"), &text_10));
        Value pattern_11;
        TRY(hosted_put_str(r, pos, &(hosted_re_find_args.pattern), &pattern_11));
        TRY(run_map_put(r, pos, q, S("pattern"), &pattern_11));
        return OK;
    }
    case HOSTED_RE_FIND_ALL: {
        Value text_12;
        TRY(hosted_put_str(r, pos, &(hosted_re_find_all_args.text), &text_12));
        TRY(run_map_put(r, pos, q, S("text"), &text_12));
        Value pattern_13;
        TRY(hosted_put_str(r, pos, &(hosted_re_find_all_args.pattern), &pattern_13));
        TRY(run_map_put(r, pos, q, S("pattern"), &pattern_13));
        return OK;
    }
    case HOSTED_RE_LINES: {
        Value text_14;
        TRY(hosted_put_str(r, pos, &(hosted_re_lines_args.text), &text_14));
        TRY(run_map_put(r, pos, q, S("text"), &text_14));
        Value pattern_15;
        TRY(hosted_put_str(r, pos, &(hosted_re_lines_args.pattern), &pattern_15));
        TRY(run_map_put(r, pos, q, S("pattern"), &pattern_15));
        return OK;
    }
    case HOSTED_RE_MATCH: {
        Value text_16;
        TRY(hosted_put_str(r, pos, &(hosted_re_match_args.text), &text_16));
        TRY(run_map_put(r, pos, q, S("text"), &text_16));
        Value pattern_17;
        TRY(hosted_put_str(r, pos, &(hosted_re_match_args.pattern), &pattern_17));
        TRY(run_map_put(r, pos, q, S("pattern"), &pattern_17));
        return OK;
    }
    case HOSTED_RE_REPLACE: {
        Value text_18;
        TRY(hosted_put_str(r, pos, &(hosted_re_replace_args.text), &text_18));
        TRY(run_map_put(r, pos, q, S("text"), &text_18));
        Value pattern_19;
        TRY(hosted_put_str(r, pos, &(hosted_re_replace_args.pattern), &pattern_19));
        TRY(run_map_put(r, pos, q, S("pattern"), &pattern_19));
        Value replacement_20;
        TRY(hosted_put_str(r, pos, &(hosted_re_replace_args.replacement), &replacement_20));
        TRY(run_map_put(r, pos, q, S("replacement"), &replacement_20));
        return OK;
    }
    case HOSTED_READ: {
        Value path_21;
        TRY(hosted_put_str(r, pos, &(hosted_read_args.path), &path_21));
        TRY(run_map_put(r, pos, q, S("path"), &path_21));
        return OK;
    }
    case HOSTED_ROC_TREE: {
        Value paths_22;
        {
            Table *list23 = NULL;
            TRY(hosted_new_list(r, pos, (hosted_roc_tree_args.paths).length, &list23));
            const RocStr *items24 = (hosted_roc_tree_args.paths).elements;
            for (size_t k25 = 0; k25 < (hosted_roc_tree_args.paths).length; k25++) {
                Value item26;
                TRY(hosted_put_str(r, pos, &(items24[k25]), &item26));
                TRY(run_list_push(r, pos, list23, &item26));
            }
            value_table(&paths_22, list23);
        }
        TRY(run_map_put(r, pos, q, S("paths"), &paths_22));
        return OK;
    }
    case HOSTED_RUN: {
        Value spec_27;
        {
            Table *map28 = NULL;
            TRY(hosted_new_map(r, pos, &map28));
            Value argv_29;
            {
                Table *list30 = NULL;
                TRY(hosted_new_list(r, pos, ((hosted_run_args.spec).argv).length, &list30));
                const RocStr *items31 = ((hosted_run_args.spec).argv).elements;
                for (size_t k32 = 0; k32 < ((hosted_run_args.spec).argv).length; k32++) {
                    Value item33;
                    TRY(hosted_put_str(r, pos, &(items31[k32]), &item33));
                    TRY(run_list_push(r, pos, list30, &item33));
                }
                value_table(&argv_29, list30);
            }
            TRY(run_map_put(r, pos, map28, S("argv"), &argv_29));
            Value keep_34;
            {
                Table *map35 = NULL;
                TRY(hosted_new_map(r, pos, &map35));
                switch (((hosted_run_args.spec).keep).tag) {
                case HeadOrTailOrWholeTag_Head: {
                    TRY(run_map_put_str(r, pos, map35, S("tag"), S("Head")));
                    uint64_t payload36 = HeadOrTailOrWhole_payload_head(&((hosted_run_args.spec).keep));
                    Value value37;
                    value_int(&value37, (int64_t)(payload36));
                    TRY(run_map_put(r, pos, map35, S("value"), &value37));
                    break;
                }
                case HeadOrTailOrWholeTag_Tail: {
                    TRY(run_map_put_str(r, pos, map35, S("tag"), S("Tail")));
                    uint64_t payload38 = HeadOrTailOrWhole_payload_tail(&((hosted_run_args.spec).keep));
                    Value value39;
                    value_int(&value39, (int64_t)(payload38));
                    TRY(run_map_put(r, pos, map35, S("value"), &value39));
                    break;
                }
                case HeadOrTailOrWholeTag_Whole: {
                    TRY(run_map_put_str(r, pos, map35, S("tag"), S("Whole")));
                    uint64_t payload40 = HeadOrTailOrWhole_payload_whole(&((hosted_run_args.spec).keep));
                    Value value41;
                    value_int(&value41, (int64_t)(payload40));
                    TRY(run_map_put(r, pos, map35, S("value"), &value41));
                    break;
                }
                default: return hosted_wrong(r, pos, S("a tag the platform declares"));
                }
                value_table(&keep_34, map35);
            }
            TRY(run_map_put(r, pos, map28, S("keep"), &keep_34));
            Value timeout_ms_42;
            value_int(&timeout_ms_42, (int64_t)((hosted_run_args.spec).timeout_ms));
            TRY(run_map_put(r, pos, map28, S("timeout_ms"), &timeout_ms_42));
            Value want_43;
            TRY(hosted_put_str(r, pos, &((hosted_run_args.spec).want), &want_43));
            TRY(run_map_put(r, pos, map28, S("want"), &want_43));
            value_table(&spec_27, map28);
        }
        TRY(run_map_put(r, pos, q, S("spec"), &spec_27));
        return OK;
    }
    case HOSTED_WRITE: {
        Value path_44;
        TRY(hosted_put_str(r, pos, &(hosted_write_args.path), &path_44));
        TRY(run_map_put(r, pos, q, S("path"), &path_44));
        Value text_45;
        TRY(hosted_put_str(r, pos, &(hosted_write_args.text), &text_45));
        TRY(run_map_put(r, pos, q, S("text"), &text_45));
        return OK;
    }
    default: return OK;
    }
}

// Shale's answer `v` to the request `kind`, as the Roc's return value.
static STATUS_MUST_USE Status hosted_answer_from(Run *r, Pos pos, int kind, const Value *v) {
    memset(&hosted_answer, 0, sizeof hosted_answer);
    switch (kind) {
    case HOSTED_ARG:
        {
            TRY(hosted_str(r, pos, v, &(hosted_answer.arg)));
        }
        return OK;
    case HOSTED_EXISTS:
        {
            TRY(hosted_bool(r, pos, v, &(hosted_answer.exists)));
        }
        return OK;
    case HOSTED_FAIL:
        (void)v;
        return OK;
    case HOSTED_GLOB:
        {
            {
                Values items46;
                TRY(hosted_items(r, pos, v, &items46));
                void *elems47 = NULL;
                TRY(hosted_list(r, pos, items46.len, sizeof(RocStr), &(hosted_answer.glob), &elems47));
                RocStr *made48 = elems47;
                for (size_t k49 = 0; k49 < items46.len; k49++) {
                    TRY(hosted_str(r, pos, &at(items46, k49), &(made48[k49])));
                }
            }
        }
        return OK;
    case HOSTED_GREP_ALL:
        {
            {
                Values items50;
                TRY(hosted_items(r, pos, v, &items50));
                void *elems51 = NULL;
                TRY(hosted_list(r, pos, items50.len, sizeof(AnonStruct1d63bdf4a74e5a0f), &(hosted_answer.grep_all), &elems51));
                AnonStruct1d63bdf4a74e5a0f *made52 = elems51;
                for (size_t k53 = 0; k53 < items50.len; k53++) {
                    {
                        Value line_54;
                        TRY(hosted_get(r, pos, &at(items50, k53), S("line"), &line_54));
                        {
                            int64_t n55 = 0;
                            TRY(hosted_int(r, pos, &line_54, &n55));
                            (made52[k53]).line = (uint64_t)n55;
                        }
                    }
                    {
                        Value text_56;
                        TRY(hosted_get(r, pos, &at(items50, k53), S("text"), &text_56));
                        TRY(hosted_str(r, pos, &text_56, &((made52[k53]).text)));
                    }
                }
            }
        }
        return OK;
    case HOSTED_LIST:
        {
            {
                Value tag57;
                TRY(hosted_get(r, pos, v, S("tag"), &tag57));
                if (tag57.tag != VALUE_STRING) return hosted_wrong(r, pos, S("tag"));
                if (str_eq(tag57.as.string, S("Found"))) {
                    Value value58;
                    TRY(hosted_get(r, pos, v, S("value"), &value58));
                    RocList payload59;
                    memset(&payload59, 0, sizeof payload59);
                    {
                        Values items60;
                        TRY(hosted_items(r, pos, &value58, &items60));
                        void *elems61 = NULL;
                        TRY(hosted_list(r, pos, items60.len, sizeof(RocStr), &(payload59), &elems61));
                        RocStr *made62 = elems61;
                        for (size_t k63 = 0; k63 < items60.len; k63++) {
                            TRY(hosted_str(r, pos, &at(items60, k63), &(made62[k63])));
                        }
                    }
                    hosted_answer.list = FoundOrMissingType7_make_found(payload59);
                } else if (str_eq(tag57.as.string, S("Missing"))) {
                    hosted_answer.list = FoundOrMissingType7_make_missing();
                } else {
                    return hosted_wrong(r, pos, S("tag"));
                }
            }
        }
        return OK;
    case HOSTED_PRINT:
        (void)v;
        return OK;
    case HOSTED_RE_FIND:
        {
            {
                Value tag64;
                TRY(hosted_get(r, pos, v, S("tag"), &tag64));
                if (tag64.tag != VALUE_STRING) return hosted_wrong(r, pos, S("tag"));
                if (str_eq(tag64.as.string, S("Found"))) {
                    Value value65;
                    TRY(hosted_get(r, pos, v, S("value"), &value65));
                    AnonStruct9ba65f4c7e87294d payload66;
                    memset(&payload66, 0, sizeof payload66);
                    {
                        Value at_67;
                        TRY(hosted_get(r, pos, &value65, S("at"), &at_67));
                        {
                            int64_t n68 = 0;
                            TRY(hosted_int(r, pos, &at_67, &n68));
                            (payload66).at = (uint64_t)n68;
                        }
                    }
                    {
                        Value text_69;
                        TRY(hosted_get(r, pos, &value65, S("text"), &text_69));
                        TRY(hosted_str(r, pos, &text_69, &((payload66).text)));
                    }
                    {
                        Value groups_70;
                        TRY(hosted_get(r, pos, &value65, S("groups"), &groups_70));
                        {
                            Values items71;
                            TRY(hosted_items(r, pos, &groups_70, &items71));
                            void *elems72 = NULL;
                            TRY(hosted_list(r, pos, items71.len, sizeof(RocStr), &((payload66).groups), &elems72));
                            RocStr *made73 = elems72;
                            for (size_t k74 = 0; k74 < items71.len; k74++) {
                                TRY(hosted_str(r, pos, &at(items71, k74), &(made73[k74])));
                            }
                        }
                    }
                    hosted_answer.re_find = FoundOrNoMatch_make_found(payload66);
                } else if (str_eq(tag64.as.string, S("NoMatch"))) {
                    hosted_answer.re_find = FoundOrNoMatch_make_no_match();
                } else {
                    return hosted_wrong(r, pos, S("tag"));
                }
            }
        }
        return OK;
    case HOSTED_RE_FIND_ALL:
        {
            {
                Values items75;
                TRY(hosted_items(r, pos, v, &items75));
                void *elems76 = NULL;
                TRY(hosted_list(r, pos, items75.len, sizeof(AnonStruct9ba65f4c7e87294d), &(hosted_answer.re_find_all), &elems76));
                AnonStruct9ba65f4c7e87294d *made77 = elems76;
                for (size_t k78 = 0; k78 < items75.len; k78++) {
                    {
                        Value at_79;
                        TRY(hosted_get(r, pos, &at(items75, k78), S("at"), &at_79));
                        {
                            int64_t n80 = 0;
                            TRY(hosted_int(r, pos, &at_79, &n80));
                            (made77[k78]).at = (uint64_t)n80;
                        }
                    }
                    {
                        Value text_81;
                        TRY(hosted_get(r, pos, &at(items75, k78), S("text"), &text_81));
                        TRY(hosted_str(r, pos, &text_81, &((made77[k78]).text)));
                    }
                    {
                        Value groups_82;
                        TRY(hosted_get(r, pos, &at(items75, k78), S("groups"), &groups_82));
                        {
                            Values items83;
                            TRY(hosted_items(r, pos, &groups_82, &items83));
                            void *elems84 = NULL;
                            TRY(hosted_list(r, pos, items83.len, sizeof(RocStr), &((made77[k78]).groups), &elems84));
                            RocStr *made85 = elems84;
                            for (size_t k86 = 0; k86 < items83.len; k86++) {
                                TRY(hosted_str(r, pos, &at(items83, k86), &(made85[k86])));
                            }
                        }
                    }
                }
            }
        }
        return OK;
    case HOSTED_RE_LINES:
        {
            {
                Values items87;
                TRY(hosted_items(r, pos, v, &items87));
                void *elems88 = NULL;
                TRY(hosted_list(r, pos, items87.len, sizeof(AnonStruct1d63bdf4a74e5a0f), &(hosted_answer.re_lines), &elems88));
                AnonStruct1d63bdf4a74e5a0f *made89 = elems88;
                for (size_t k90 = 0; k90 < items87.len; k90++) {
                    {
                        Value line_91;
                        TRY(hosted_get(r, pos, &at(items87, k90), S("line"), &line_91));
                        {
                            int64_t n92 = 0;
                            TRY(hosted_int(r, pos, &line_91, &n92));
                            (made89[k90]).line = (uint64_t)n92;
                        }
                    }
                    {
                        Value text_93;
                        TRY(hosted_get(r, pos, &at(items87, k90), S("text"), &text_93));
                        TRY(hosted_str(r, pos, &text_93, &((made89[k90]).text)));
                    }
                }
            }
        }
        return OK;
    case HOSTED_RE_MATCH:
        {
            TRY(hosted_bool(r, pos, v, &(hosted_answer.re_match)));
        }
        return OK;
    case HOSTED_RE_REPLACE:
        {
            TRY(hosted_str(r, pos, v, &(hosted_answer.re_replace)));
        }
        return OK;
    case HOSTED_READ:
        {
            {
                Value tag94;
                TRY(hosted_get(r, pos, v, S("tag"), &tag94));
                if (tag94.tag != VALUE_STRING) return hosted_wrong(r, pos, S("tag"));
                if (str_eq(tag94.as.string, S("Found"))) {
                    Value value95;
                    TRY(hosted_get(r, pos, v, S("value"), &value95));
                    RocStr payload96;
                    memset(&payload96, 0, sizeof payload96);
                    TRY(hosted_str(r, pos, &value95, &(payload96)));
                    hosted_answer.read = FoundOrMissingType11_make_found(payload96);
                } else if (str_eq(tag94.as.string, S("Missing"))) {
                    hosted_answer.read = FoundOrMissingType11_make_missing();
                } else {
                    return hosted_wrong(r, pos, S("tag"));
                }
            }
        }
        return OK;
    case HOSTED_ROC_TREE:
        {
            {
                Values items97;
                TRY(hosted_items(r, pos, v, &items97));
                void *elems98 = NULL;
                TRY(hosted_list(r, pos, items97.len, sizeof(AnonStruct8e43eed7b6bbf36f), &(hosted_answer.roc_tree), &elems98));
                AnonStruct8e43eed7b6bbf36f *made99 = elems98;
                for (size_t k100 = 0; k100 < items97.len; k100++) {
                    {
                        Value path_101;
                        TRY(hosted_get(r, pos, &at(items97, k100), S("path"), &path_101));
                        TRY(hosted_str(r, pos, &path_101, &((made99[k100]).path)));
                    }
                    {
                        Value err_102;
                        TRY(hosted_get(r, pos, &at(items97, k100), S("err"), &err_102));
                        TRY(hosted_str(r, pos, &err_102, &((made99[k100]).err)));
                    }
                    {
                        Value nodes_103;
                        TRY(hosted_get(r, pos, &at(items97, k100), S("nodes"), &nodes_103));
                        {
                            Values items104;
                            TRY(hosted_items(r, pos, &nodes_103, &items104));
                            void *elems105 = NULL;
                            TRY(hosted_list(r, pos, items104.len, sizeof(AnonStructCab41e0e3e24b946), &((made99[k100]).nodes), &elems105));
                            AnonStructCab41e0e3e24b946 *made106 = elems105;
                            for (size_t k107 = 0; k107 < items104.len; k107++) {
                                {
                                    Value kind_108;
                                    TRY(hosted_get(r, pos, &at(items104, k107), S("kind"), &kind_108));
                                    TRY(hosted_str(r, pos, &kind_108, &((made106[k107]).kind)));
                                }
                                {
                                    Value depth_109;
                                    TRY(hosted_get(r, pos, &at(items104, k107), S("depth"), &depth_109));
                                    {
                                        int64_t n110 = 0;
                                        TRY(hosted_int(r, pos, &depth_109, &n110));
                                        (made106[k107]).depth = (uint64_t)n110;
                                    }
                                }
                                {
                                    Value parent_111;
                                    TRY(hosted_get(r, pos, &at(items104, k107), S("parent"), &parent_111));
                                    {
                                        int64_t n112 = 0;
                                        TRY(hosted_int(r, pos, &parent_111, &n112));
                                        (made106[k107]).parent = (uint64_t)n112;
                                    }
                                }
                                {
                                    Value kids_113;
                                    TRY(hosted_get(r, pos, &at(items104, k107), S("kids"), &kids_113));
                                    {
                                        Values items114;
                                        TRY(hosted_items(r, pos, &kids_113, &items114));
                                        void *elems115 = NULL;
                                        TRY(hosted_list(r, pos, items114.len, sizeof(uint64_t), &((made106[k107]).kids), &elems115));
                                        uint64_t *made116 = elems115;
                                        for (size_t k117 = 0; k117 < items114.len; k117++) {
                                            {
                                                int64_t n118 = 0;
                                                TRY(hosted_int(r, pos, &at(items114, k117), &n118));
                                                made116[k117] = (uint64_t)n118;
                                            }
                                        }
                                    }
                                }
                                {
                                    Value line_119;
                                    TRY(hosted_get(r, pos, &at(items104, k107), S("line"), &line_119));
                                    {
                                        int64_t n120 = 0;
                                        TRY(hosted_int(r, pos, &line_119, &n120));
                                        (made106[k107]).line = (uint64_t)n120;
                                    }
                                }
                                {
                                    Value col_121;
                                    TRY(hosted_get(r, pos, &at(items104, k107), S("col"), &col_121));
                                    {
                                        int64_t n122 = 0;
                                        TRY(hosted_int(r, pos, &col_121, &n122));
                                        (made106[k107]).col = (uint64_t)n122;
                                    }
                                }
                                {
                                    Value end_line_123;
                                    TRY(hosted_get(r, pos, &at(items104, k107), S("end_line"), &end_line_123));
                                    {
                                        int64_t n124 = 0;
                                        TRY(hosted_int(r, pos, &end_line_123, &n124));
                                        (made106[k107]).end_line = (uint64_t)n124;
                                    }
                                }
                                {
                                    Value end_col_125;
                                    TRY(hosted_get(r, pos, &at(items104, k107), S("end_col"), &end_col_125));
                                    {
                                        int64_t n126 = 0;
                                        TRY(hosted_int(r, pos, &end_col_125, &n126));
                                        (made106[k107]).end_col = (uint64_t)n126;
                                    }
                                }
                                {
                                    Value attrs_127;
                                    TRY(hosted_get(r, pos, &at(items104, k107), S("attrs"), &attrs_127));
                                    {
                                        Values items128;
                                        TRY(hosted_items(r, pos, &attrs_127, &items128));
                                        void *elems129 = NULL;
                                        TRY(hosted_list(r, pos, items128.len, sizeof(AnonStruct82a96c5d55d63488), &((made106[k107]).attrs), &elems129));
                                        AnonStruct82a96c5d55d63488 *made130 = elems129;
                                        for (size_t k131 = 0; k131 < items128.len; k131++) {
                                            {
                                                Value name_132;
                                                TRY(hosted_get(r, pos, &at(items128, k131), S("name"), &name_132));
                                                TRY(hosted_str(r, pos, &name_132, &((made130[k131]).name)));
                                            }
                                            {
                                                Value value_133;
                                                TRY(hosted_get(r, pos, &at(items128, k131), S("value"), &value_133));
                                                TRY(hosted_str(r, pos, &value_133, &((made130[k131]).value)));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        return OK;
    case HOSTED_RUN:
        {
            {
                Value code_134;
                TRY(hosted_get(r, pos, v, S("code"), &code_134));
                {
                    int64_t n135 = 0;
                    TRY(hosted_int(r, pos, &code_134, &n135));
                    (hosted_answer.run).code = (int64_t)n135;
                }
            }
            {
                Value out_136;
                TRY(hosted_get(r, pos, v, S("out"), &out_136));
                TRY(hosted_str(r, pos, &out_136, &((hosted_answer.run).out)));
            }
        }
        return OK;
    case HOSTED_WRITE:
        (void)v;
        return OK;
    default: break;
    }
    return hosted_wrong(r, pos, S("a request it asked"));
}
