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
    HOSTED_MKDIR,
    HOSTED_PRINT,
    HOSTED_RE_FIND,
    HOSTED_RE_FIND_ALL,
    HOSTED_RE_LINES,
    HOSTED_RE_MATCH,
    HOSTED_RE_REPLACE,
    HOSTED_READ,
    HOSTED_REAL,
    HOSTED_REMOVE_TREE,
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
    S_ITEM("mkdir"),
    S_ITEM("print"),
    S_ITEM("re_find"),
    S_ITEM("re_find_all"),
    S_ITEM("re_lines"),
    S_ITEM("re_match"),
    S_ITEM("re_replace"),
    S_ITEM("read"),
    S_ITEM("real"),
    S_ITEM("remove_tree"),
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
    S_ITEM("Shale.mkdir!"),
    S_ITEM("Shale.print!"),
    S_ITEM("Shale.re_find!"),
    S_ITEM("Shale.re_find_all!"),
    S_ITEM("Shale.re_lines!"),
    S_ITEM("Shale.re_match!"),
    S_ITEM("Shale.re_replace!"),
    S_ITEM("Shale.read!"),
    S_ITEM("Shale.real!"),
    S_ITEM("Shale.remove_tree!"),
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
    "function hosted_mkdir(q)\n"
    "  mkdir(q.path)\n"
    "  return {}\n"
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
    "function hosted_real(q)\n"
    "  return real(q.path)\n"
    "end\n"
    "function hosted_remove_tree(q)\n"
    "  remove_tree(q.path)\n"
    "  return {}\n"
    "end\n"
    "function hosted_roc_tree(q)\n"
    "  return roc_tree(q.paths)\n"
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
    "  if f == \"mkdir\" then return hosted_mkdir(q) end\n"
    "  if f == \"print\" then return hosted_print(q) end\n"
    "  if f == \"re_find\" then return hosted_re_find(q) end\n"
    "  if f == \"re_find_all\" then return hosted_re_find_all(q) end\n"
    "  if f == \"re_lines\" then return hosted_re_lines(q) end\n"
    "  if f == \"re_match\" then return hosted_re_match(q) end\n"
    "  if f == \"re_replace\" then return hosted_re_replace(q) end\n"
    "  if f == \"read\" then return hosted_read(q) end\n"
    "  if f == \"real\" then return hosted_real(q) end\n"
    "  if f == \"remove_tree\" then return hosted_remove_tree(q) end\n"
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
    RocStr path;
} hosted_mkdir_args;
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
    RocStr path;
} hosted_real_args;
static struct {
    RocStr path;
} hosted_remove_tree_args;
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
    RocStr real;
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

void shale_mkdir(RocStr arg0) {
    hosted_mkdir_args.path = arg0;
    roc_ask(HOSTED_MKDIR, __builtin_return_address(0));
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

RocStr shale_real(RocStr arg0) {
    hosted_real_args.path = arg0;
    roc_ask(HOSTED_REAL, __builtin_return_address(0));
    return hosted_answer.real;
}

void shale_remove_tree(RocStr arg0) {
    hosted_remove_tree_args.path = arg0;
    roc_ask(HOSTED_REMOVE_TREE, __builtin_return_address(0));
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
    case HOSTED_MKDIR: {
        Value path_9;
        TRY(hosted_put_str(r, pos, &(hosted_mkdir_args.path), &path_9));
        TRY(run_map_put(r, pos, q, S("path"), &path_9));
        return OK;
    }
    case HOSTED_PRINT: {
        Value text_10;
        TRY(hosted_put_str(r, pos, &(hosted_print_args.text), &text_10));
        TRY(run_map_put(r, pos, q, S("text"), &text_10));
        return OK;
    }
    case HOSTED_RE_FIND: {
        Value text_11;
        TRY(hosted_put_str(r, pos, &(hosted_re_find_args.text), &text_11));
        TRY(run_map_put(r, pos, q, S("text"), &text_11));
        Value pattern_12;
        TRY(hosted_put_str(r, pos, &(hosted_re_find_args.pattern), &pattern_12));
        TRY(run_map_put(r, pos, q, S("pattern"), &pattern_12));
        return OK;
    }
    case HOSTED_RE_FIND_ALL: {
        Value text_13;
        TRY(hosted_put_str(r, pos, &(hosted_re_find_all_args.text), &text_13));
        TRY(run_map_put(r, pos, q, S("text"), &text_13));
        Value pattern_14;
        TRY(hosted_put_str(r, pos, &(hosted_re_find_all_args.pattern), &pattern_14));
        TRY(run_map_put(r, pos, q, S("pattern"), &pattern_14));
        return OK;
    }
    case HOSTED_RE_LINES: {
        Value text_15;
        TRY(hosted_put_str(r, pos, &(hosted_re_lines_args.text), &text_15));
        TRY(run_map_put(r, pos, q, S("text"), &text_15));
        Value pattern_16;
        TRY(hosted_put_str(r, pos, &(hosted_re_lines_args.pattern), &pattern_16));
        TRY(run_map_put(r, pos, q, S("pattern"), &pattern_16));
        return OK;
    }
    case HOSTED_RE_MATCH: {
        Value text_17;
        TRY(hosted_put_str(r, pos, &(hosted_re_match_args.text), &text_17));
        TRY(run_map_put(r, pos, q, S("text"), &text_17));
        Value pattern_18;
        TRY(hosted_put_str(r, pos, &(hosted_re_match_args.pattern), &pattern_18));
        TRY(run_map_put(r, pos, q, S("pattern"), &pattern_18));
        return OK;
    }
    case HOSTED_RE_REPLACE: {
        Value text_19;
        TRY(hosted_put_str(r, pos, &(hosted_re_replace_args.text), &text_19));
        TRY(run_map_put(r, pos, q, S("text"), &text_19));
        Value pattern_20;
        TRY(hosted_put_str(r, pos, &(hosted_re_replace_args.pattern), &pattern_20));
        TRY(run_map_put(r, pos, q, S("pattern"), &pattern_20));
        Value replacement_21;
        TRY(hosted_put_str(r, pos, &(hosted_re_replace_args.replacement), &replacement_21));
        TRY(run_map_put(r, pos, q, S("replacement"), &replacement_21));
        return OK;
    }
    case HOSTED_READ: {
        Value path_22;
        TRY(hosted_put_str(r, pos, &(hosted_read_args.path), &path_22));
        TRY(run_map_put(r, pos, q, S("path"), &path_22));
        return OK;
    }
    case HOSTED_REAL: {
        Value path_23;
        TRY(hosted_put_str(r, pos, &(hosted_real_args.path), &path_23));
        TRY(run_map_put(r, pos, q, S("path"), &path_23));
        return OK;
    }
    case HOSTED_REMOVE_TREE: {
        Value path_24;
        TRY(hosted_put_str(r, pos, &(hosted_remove_tree_args.path), &path_24));
        TRY(run_map_put(r, pos, q, S("path"), &path_24));
        return OK;
    }
    case HOSTED_ROC_TREE: {
        Value paths_25;
        {
            Table *list26 = NULL;
            TRY(hosted_new_list(r, pos, (hosted_roc_tree_args.paths).length, &list26));
            const RocStr *items27 = (hosted_roc_tree_args.paths).elements;
            for (size_t k28 = 0; k28 < (hosted_roc_tree_args.paths).length; k28++) {
                Value item29;
                TRY(hosted_put_str(r, pos, &(items27[k28]), &item29));
                TRY(run_list_push(r, pos, list26, &item29));
            }
            value_table(&paths_25, list26);
        }
        TRY(run_map_put(r, pos, q, S("paths"), &paths_25));
        return OK;
    }
    case HOSTED_RUN: {
        Value spec_30;
        {
            Table *map31 = NULL;
            TRY(hosted_new_map(r, pos, &map31));
            Value argv_32;
            {
                Table *list33 = NULL;
                TRY(hosted_new_list(r, pos, ((hosted_run_args.spec).argv).length, &list33));
                const RocStr *items34 = ((hosted_run_args.spec).argv).elements;
                for (size_t k35 = 0; k35 < ((hosted_run_args.spec).argv).length; k35++) {
                    Value item36;
                    TRY(hosted_put_str(r, pos, &(items34[k35]), &item36));
                    TRY(run_list_push(r, pos, list33, &item36));
                }
                value_table(&argv_32, list33);
            }
            TRY(run_map_put(r, pos, map31, S("argv"), &argv_32));
            Value keep_37;
            {
                Table *map38 = NULL;
                TRY(hosted_new_map(r, pos, &map38));
                switch (((hosted_run_args.spec).keep).tag) {
                case HeadOrTailOrWholeTag_Head: {
                    TRY(run_map_put_str(r, pos, map38, S("tag"), S("Head")));
                    uint64_t payload39 = HeadOrTailOrWhole_payload_head(&((hosted_run_args.spec).keep));
                    Value value40;
                    value_int(&value40, (int64_t)(payload39));
                    TRY(run_map_put(r, pos, map38, S("value"), &value40));
                    break;
                }
                case HeadOrTailOrWholeTag_Tail: {
                    TRY(run_map_put_str(r, pos, map38, S("tag"), S("Tail")));
                    uint64_t payload41 = HeadOrTailOrWhole_payload_tail(&((hosted_run_args.spec).keep));
                    Value value42;
                    value_int(&value42, (int64_t)(payload41));
                    TRY(run_map_put(r, pos, map38, S("value"), &value42));
                    break;
                }
                case HeadOrTailOrWholeTag_Whole: {
                    TRY(run_map_put_str(r, pos, map38, S("tag"), S("Whole")));
                    uint64_t payload43 = HeadOrTailOrWhole_payload_whole(&((hosted_run_args.spec).keep));
                    Value value44;
                    value_int(&value44, (int64_t)(payload43));
                    TRY(run_map_put(r, pos, map38, S("value"), &value44));
                    break;
                }
                default: return hosted_wrong(r, pos, S("a tag the platform declares"));
                }
                value_table(&keep_37, map38);
            }
            TRY(run_map_put(r, pos, map31, S("keep"), &keep_37));
            Value timeout_ms_45;
            value_int(&timeout_ms_45, (int64_t)((hosted_run_args.spec).timeout_ms));
            TRY(run_map_put(r, pos, map31, S("timeout_ms"), &timeout_ms_45));
            Value want_46;
            TRY(hosted_put_str(r, pos, &((hosted_run_args.spec).want), &want_46));
            TRY(run_map_put(r, pos, map31, S("want"), &want_46));
            value_table(&spec_30, map31);
        }
        TRY(run_map_put(r, pos, q, S("spec"), &spec_30));
        return OK;
    }
    case HOSTED_WRITE: {
        Value path_47;
        TRY(hosted_put_str(r, pos, &(hosted_write_args.path), &path_47));
        TRY(run_map_put(r, pos, q, S("path"), &path_47));
        Value text_48;
        TRY(hosted_put_str(r, pos, &(hosted_write_args.text), &text_48));
        TRY(run_map_put(r, pos, q, S("text"), &text_48));
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
                Values items49;
                TRY(hosted_items(r, pos, v, &items49));
                void *elems50 = NULL;
                TRY(hosted_list(r, pos, items49.len, sizeof(RocStr), &(hosted_answer.glob), &elems50));
                RocStr *made51 = elems50;
                for (size_t k52 = 0; k52 < items49.len; k52++) {
                    TRY(hosted_str(r, pos, &at(items49, k52), &(made51[k52])));
                }
            }
        }
        return OK;
    case HOSTED_GREP_ALL:
        {
            {
                Values items53;
                TRY(hosted_items(r, pos, v, &items53));
                void *elems54 = NULL;
                TRY(hosted_list(r, pos, items53.len, sizeof(AnonStruct1d63bdf4a74e5a0f), &(hosted_answer.grep_all), &elems54));
                AnonStruct1d63bdf4a74e5a0f *made55 = elems54;
                for (size_t k56 = 0; k56 < items53.len; k56++) {
                    {
                        Value line_57;
                        TRY(hosted_get(r, pos, &at(items53, k56), S("line"), &line_57));
                        {
                            int64_t n58 = 0;
                            TRY(hosted_int(r, pos, &line_57, &n58));
                            (made55[k56]).line = (uint64_t)n58;
                        }
                    }
                    {
                        Value text_59;
                        TRY(hosted_get(r, pos, &at(items53, k56), S("text"), &text_59));
                        TRY(hosted_str(r, pos, &text_59, &((made55[k56]).text)));
                    }
                }
            }
        }
        return OK;
    case HOSTED_LIST:
        {
            {
                Value tag60;
                TRY(hosted_get(r, pos, v, S("tag"), &tag60));
                if (tag60.tag != VALUE_STRING) return hosted_wrong(r, pos, S("tag"));
                if (str_eq(tag60.as.string, S("Found"))) {
                    Value value61;
                    TRY(hosted_get(r, pos, v, S("value"), &value61));
                    RocList payload62;
                    memset(&payload62, 0, sizeof payload62);
                    {
                        Values items63;
                        TRY(hosted_items(r, pos, &value61, &items63));
                        void *elems64 = NULL;
                        TRY(hosted_list(r, pos, items63.len, sizeof(RocStr), &(payload62), &elems64));
                        RocStr *made65 = elems64;
                        for (size_t k66 = 0; k66 < items63.len; k66++) {
                            TRY(hosted_str(r, pos, &at(items63, k66), &(made65[k66])));
                        }
                    }
                    hosted_answer.list = FoundOrMissingType7_make_found(payload62);
                } else if (str_eq(tag60.as.string, S("Missing"))) {
                    hosted_answer.list = FoundOrMissingType7_make_missing();
                } else {
                    return hosted_wrong(r, pos, S("tag"));
                }
            }
        }
        return OK;
    case HOSTED_MKDIR:
        (void)v;
        return OK;
    case HOSTED_PRINT:
        (void)v;
        return OK;
    case HOSTED_RE_FIND:
        {
            {
                Value tag67;
                TRY(hosted_get(r, pos, v, S("tag"), &tag67));
                if (tag67.tag != VALUE_STRING) return hosted_wrong(r, pos, S("tag"));
                if (str_eq(tag67.as.string, S("Found"))) {
                    Value value68;
                    TRY(hosted_get(r, pos, v, S("value"), &value68));
                    AnonStruct9ba65f4c7e87294d payload69;
                    memset(&payload69, 0, sizeof payload69);
                    {
                        Value at_70;
                        TRY(hosted_get(r, pos, &value68, S("at"), &at_70));
                        {
                            int64_t n71 = 0;
                            TRY(hosted_int(r, pos, &at_70, &n71));
                            (payload69).at = (uint64_t)n71;
                        }
                    }
                    {
                        Value text_72;
                        TRY(hosted_get(r, pos, &value68, S("text"), &text_72));
                        TRY(hosted_str(r, pos, &text_72, &((payload69).text)));
                    }
                    {
                        Value groups_73;
                        TRY(hosted_get(r, pos, &value68, S("groups"), &groups_73));
                        {
                            Values items74;
                            TRY(hosted_items(r, pos, &groups_73, &items74));
                            void *elems75 = NULL;
                            TRY(hosted_list(r, pos, items74.len, sizeof(RocStr), &((payload69).groups), &elems75));
                            RocStr *made76 = elems75;
                            for (size_t k77 = 0; k77 < items74.len; k77++) {
                                TRY(hosted_str(r, pos, &at(items74, k77), &(made76[k77])));
                            }
                        }
                    }
                    hosted_answer.re_find = FoundOrNoMatch_make_found(payload69);
                } else if (str_eq(tag67.as.string, S("NoMatch"))) {
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
                Values items78;
                TRY(hosted_items(r, pos, v, &items78));
                void *elems79 = NULL;
                TRY(hosted_list(r, pos, items78.len, sizeof(AnonStruct9ba65f4c7e87294d), &(hosted_answer.re_find_all), &elems79));
                AnonStruct9ba65f4c7e87294d *made80 = elems79;
                for (size_t k81 = 0; k81 < items78.len; k81++) {
                    {
                        Value at_82;
                        TRY(hosted_get(r, pos, &at(items78, k81), S("at"), &at_82));
                        {
                            int64_t n83 = 0;
                            TRY(hosted_int(r, pos, &at_82, &n83));
                            (made80[k81]).at = (uint64_t)n83;
                        }
                    }
                    {
                        Value text_84;
                        TRY(hosted_get(r, pos, &at(items78, k81), S("text"), &text_84));
                        TRY(hosted_str(r, pos, &text_84, &((made80[k81]).text)));
                    }
                    {
                        Value groups_85;
                        TRY(hosted_get(r, pos, &at(items78, k81), S("groups"), &groups_85));
                        {
                            Values items86;
                            TRY(hosted_items(r, pos, &groups_85, &items86));
                            void *elems87 = NULL;
                            TRY(hosted_list(r, pos, items86.len, sizeof(RocStr), &((made80[k81]).groups), &elems87));
                            RocStr *made88 = elems87;
                            for (size_t k89 = 0; k89 < items86.len; k89++) {
                                TRY(hosted_str(r, pos, &at(items86, k89), &(made88[k89])));
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
                Values items90;
                TRY(hosted_items(r, pos, v, &items90));
                void *elems91 = NULL;
                TRY(hosted_list(r, pos, items90.len, sizeof(AnonStruct1d63bdf4a74e5a0f), &(hosted_answer.re_lines), &elems91));
                AnonStruct1d63bdf4a74e5a0f *made92 = elems91;
                for (size_t k93 = 0; k93 < items90.len; k93++) {
                    {
                        Value line_94;
                        TRY(hosted_get(r, pos, &at(items90, k93), S("line"), &line_94));
                        {
                            int64_t n95 = 0;
                            TRY(hosted_int(r, pos, &line_94, &n95));
                            (made92[k93]).line = (uint64_t)n95;
                        }
                    }
                    {
                        Value text_96;
                        TRY(hosted_get(r, pos, &at(items90, k93), S("text"), &text_96));
                        TRY(hosted_str(r, pos, &text_96, &((made92[k93]).text)));
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
                Value tag97;
                TRY(hosted_get(r, pos, v, S("tag"), &tag97));
                if (tag97.tag != VALUE_STRING) return hosted_wrong(r, pos, S("tag"));
                if (str_eq(tag97.as.string, S("Found"))) {
                    Value value98;
                    TRY(hosted_get(r, pos, v, S("value"), &value98));
                    RocStr payload99;
                    memset(&payload99, 0, sizeof payload99);
                    TRY(hosted_str(r, pos, &value98, &(payload99)));
                    hosted_answer.read = FoundOrMissingType11_make_found(payload99);
                } else if (str_eq(tag97.as.string, S("Missing"))) {
                    hosted_answer.read = FoundOrMissingType11_make_missing();
                } else {
                    return hosted_wrong(r, pos, S("tag"));
                }
            }
        }
        return OK;
    case HOSTED_REAL:
        {
            TRY(hosted_str(r, pos, v, &(hosted_answer.real)));
        }
        return OK;
    case HOSTED_REMOVE_TREE:
        (void)v;
        return OK;
    case HOSTED_ROC_TREE:
        {
            {
                Values items100;
                TRY(hosted_items(r, pos, v, &items100));
                void *elems101 = NULL;
                TRY(hosted_list(r, pos, items100.len, sizeof(AnonStruct8e43eed7b6bbf36f), &(hosted_answer.roc_tree), &elems101));
                AnonStruct8e43eed7b6bbf36f *made102 = elems101;
                for (size_t k103 = 0; k103 < items100.len; k103++) {
                    {
                        Value path_104;
                        TRY(hosted_get(r, pos, &at(items100, k103), S("path"), &path_104));
                        TRY(hosted_str(r, pos, &path_104, &((made102[k103]).path)));
                    }
                    {
                        Value err_105;
                        TRY(hosted_get(r, pos, &at(items100, k103), S("err"), &err_105));
                        TRY(hosted_str(r, pos, &err_105, &((made102[k103]).err)));
                    }
                    {
                        Value nodes_106;
                        TRY(hosted_get(r, pos, &at(items100, k103), S("nodes"), &nodes_106));
                        {
                            Values items107;
                            TRY(hosted_items(r, pos, &nodes_106, &items107));
                            void *elems108 = NULL;
                            TRY(hosted_list(r, pos, items107.len, sizeof(AnonStructCab41e0e3e24b946), &((made102[k103]).nodes), &elems108));
                            AnonStructCab41e0e3e24b946 *made109 = elems108;
                            for (size_t k110 = 0; k110 < items107.len; k110++) {
                                {
                                    Value kind_111;
                                    TRY(hosted_get(r, pos, &at(items107, k110), S("kind"), &kind_111));
                                    TRY(hosted_str(r, pos, &kind_111, &((made109[k110]).kind)));
                                }
                                {
                                    Value depth_112;
                                    TRY(hosted_get(r, pos, &at(items107, k110), S("depth"), &depth_112));
                                    {
                                        int64_t n113 = 0;
                                        TRY(hosted_int(r, pos, &depth_112, &n113));
                                        (made109[k110]).depth = (uint64_t)n113;
                                    }
                                }
                                {
                                    Value parent_114;
                                    TRY(hosted_get(r, pos, &at(items107, k110), S("parent"), &parent_114));
                                    {
                                        int64_t n115 = 0;
                                        TRY(hosted_int(r, pos, &parent_114, &n115));
                                        (made109[k110]).parent = (uint64_t)n115;
                                    }
                                }
                                {
                                    Value kids_116;
                                    TRY(hosted_get(r, pos, &at(items107, k110), S("kids"), &kids_116));
                                    {
                                        Values items117;
                                        TRY(hosted_items(r, pos, &kids_116, &items117));
                                        void *elems118 = NULL;
                                        TRY(hosted_list(r, pos, items117.len, sizeof(uint64_t), &((made109[k110]).kids), &elems118));
                                        uint64_t *made119 = elems118;
                                        for (size_t k120 = 0; k120 < items117.len; k120++) {
                                            {
                                                int64_t n121 = 0;
                                                TRY(hosted_int(r, pos, &at(items117, k120), &n121));
                                                made119[k120] = (uint64_t)n121;
                                            }
                                        }
                                    }
                                }
                                {
                                    Value line_122;
                                    TRY(hosted_get(r, pos, &at(items107, k110), S("line"), &line_122));
                                    {
                                        int64_t n123 = 0;
                                        TRY(hosted_int(r, pos, &line_122, &n123));
                                        (made109[k110]).line = (uint64_t)n123;
                                    }
                                }
                                {
                                    Value col_124;
                                    TRY(hosted_get(r, pos, &at(items107, k110), S("col"), &col_124));
                                    {
                                        int64_t n125 = 0;
                                        TRY(hosted_int(r, pos, &col_124, &n125));
                                        (made109[k110]).col = (uint64_t)n125;
                                    }
                                }
                                {
                                    Value end_line_126;
                                    TRY(hosted_get(r, pos, &at(items107, k110), S("end_line"), &end_line_126));
                                    {
                                        int64_t n127 = 0;
                                        TRY(hosted_int(r, pos, &end_line_126, &n127));
                                        (made109[k110]).end_line = (uint64_t)n127;
                                    }
                                }
                                {
                                    Value end_col_128;
                                    TRY(hosted_get(r, pos, &at(items107, k110), S("end_col"), &end_col_128));
                                    {
                                        int64_t n129 = 0;
                                        TRY(hosted_int(r, pos, &end_col_128, &n129));
                                        (made109[k110]).end_col = (uint64_t)n129;
                                    }
                                }
                                {
                                    if ((&at(items107, k110))->tag != VALUE_TABLE || (&at(items107, k110))->as.table->kind != TABLE_MAP) return hosted_wrong(r, pos, S("a map"));
                                    const ValueMap *map130 = &(&at(items107, k110))->as.table->map;
                                    size_t n131 = 0;
                                    for (size_t k = 0; k < ValueMap_len(map130); k++) {
                                        Str key = ValueMap_key_at(map130, k);
                                        if (!(str_eq(key, S("kind")) || str_eq(key, S("depth")) || str_eq(key, S("parent")) || str_eq(key, S("kids")) || str_eq(key, S("line")) || str_eq(key, S("col")) || str_eq(key, S("end_line")) || str_eq(key, S("end_col")))) n131++;
                                    }
                                    void *elems132 = NULL;
                                    TRY(hosted_list(r, pos, n131, sizeof(AnonStruct82a96c5d55d63488), &((made109[k110]).attrs), &elems132));
                                    AnonStruct82a96c5d55d63488 *made133 = elems132;
                                    size_t j = 0;
                                    for (size_t k = 0; k < ValueMap_len(map130); k++) {
                                        Str key = ValueMap_key_at(map130, k);
                                        if (str_eq(key, S("kind")) || str_eq(key, S("depth")) || str_eq(key, S("parent")) || str_eq(key, S("kids")) || str_eq(key, S("line")) || str_eq(key, S("col")) || str_eq(key, S("end_line")) || str_eq(key, S("end_col"))) continue;
                                        Value name_v;
                                        value_string(&name_v, key);
                                        Value value_v = ValueMap_value_at(map130, k);
                                        TRY(hosted_str(r, pos, &name_v, &made133[j].name));
                                        TRY(hosted_str(r, pos, &value_v, &made133[j].value));
                                        j++;
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
