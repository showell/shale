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
    HOSTED_READ,
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
    S_ITEM("read"),
    S_ITEM("run"),
    S_ITEM("write"),
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
    "function hosted_read(q)\n"
    "  if not exists(q.path) then return {tag = \"Missing\"} end\n"
    "  return {tag = \"Found\", value = read(q.path)}\n"
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
    "  if f == \"read\" then return hosted_read(q) end\n"
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
    RocStr path;
} hosted_read_args;
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
    FoundOrMissingType8 read;
    AnonStruct1763995a077e9c1f run;
} hosted_answer;

// ── the shims: the Roc's side of each hosted function ──

RocStr shale_arg(RocStr arg0, RocStr arg1) {
    hosted_arg_args.name = arg0;
    hosted_arg_args.fallback = arg1;
    roc_ask(HOSTED_ARG);
    return hosted_answer.arg;
}

bool shale_exists(RocStr arg0) {
    hosted_exists_args.path = arg0;
    roc_ask(HOSTED_EXISTS);
    return hosted_answer.exists;
}

void shale_fail(RocStr arg0) {
    hosted_fail_args.why = arg0;
    roc_ask(HOSTED_FAIL);
}

RocList shale_glob(RocStr arg0) {
    hosted_glob_args.pattern = arg0;
    roc_ask(HOSTED_GLOB);
    return hosted_answer.glob;
}

RocList shale_grep_all(RocStr arg0, RocStr arg1) {
    hosted_grep_all_args.path = arg0;
    hosted_grep_all_args.literal = arg1;
    roc_ask(HOSTED_GREP_ALL);
    return hosted_answer.grep_all;
}

FoundOrMissingType7 shale_list(RocStr arg0) {
    hosted_list_args.path = arg0;
    roc_ask(HOSTED_LIST);
    return hosted_answer.list;
}

void shale_print(RocStr arg0) {
    hosted_print_args.text = arg0;
    roc_ask(HOSTED_PRINT);
}

FoundOrMissingType8 shale_read(RocStr arg0) {
    hosted_read_args.path = arg0;
    roc_ask(HOSTED_READ);
    return hosted_answer.read;
}

AnonStruct1763995a077e9c1f shale_run(ShaleRunArgs arg0) {
    hosted_run_args.spec = arg0;
    roc_ask(HOSTED_RUN);
    return hosted_answer.run;
}

void shale_write(RocStr arg0, RocStr arg1) {
    hosted_write_args.path = arg0;
    hosted_write_args.text = arg1;
    roc_ask(HOSTED_WRITE);
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
    case HOSTED_READ: {
        Value path_10;
        TRY(hosted_put_str(r, pos, &(hosted_read_args.path), &path_10));
        TRY(run_map_put(r, pos, q, S("path"), &path_10));
        return OK;
    }
    case HOSTED_RUN: {
        Value spec_11;
        {
            Table *map12 = NULL;
            TRY(hosted_new_map(r, pos, &map12));
            Value argv_13;
            {
                Table *list14 = NULL;
                TRY(hosted_new_list(r, pos, ((hosted_run_args.spec).argv).length, &list14));
                const RocStr *items15 = ((hosted_run_args.spec).argv).elements;
                for (size_t k16 = 0; k16 < ((hosted_run_args.spec).argv).length; k16++) {
                    Value item17;
                    TRY(hosted_put_str(r, pos, &(items15[k16]), &item17));
                    TRY(run_list_push(r, pos, list14, &item17));
                }
                value_table(&argv_13, list14);
            }
            TRY(run_map_put(r, pos, map12, S("argv"), &argv_13));
            Value keep_18;
            {
                Table *map19 = NULL;
                TRY(hosted_new_map(r, pos, &map19));
                switch (((hosted_run_args.spec).keep).tag) {
                case HeadOrTailOrWholeTag_Head: {
                    TRY(run_map_put_str(r, pos, map19, S("tag"), S("Head")));
                    uint64_t payload20 = HeadOrTailOrWhole_payload_head(&((hosted_run_args.spec).keep));
                    Value value21;
                    value_int(&value21, (int64_t)(payload20));
                    TRY(run_map_put(r, pos, map19, S("value"), &value21));
                    break;
                }
                case HeadOrTailOrWholeTag_Tail: {
                    TRY(run_map_put_str(r, pos, map19, S("tag"), S("Tail")));
                    uint64_t payload22 = HeadOrTailOrWhole_payload_tail(&((hosted_run_args.spec).keep));
                    Value value23;
                    value_int(&value23, (int64_t)(payload22));
                    TRY(run_map_put(r, pos, map19, S("value"), &value23));
                    break;
                }
                case HeadOrTailOrWholeTag_Whole: {
                    TRY(run_map_put_str(r, pos, map19, S("tag"), S("Whole")));
                    uint64_t payload24 = HeadOrTailOrWhole_payload_whole(&((hosted_run_args.spec).keep));
                    Value value25;
                    value_int(&value25, (int64_t)(payload24));
                    TRY(run_map_put(r, pos, map19, S("value"), &value25));
                    break;
                }
                default: return hosted_wrong(r, pos, S("a tag the platform declares"));
                }
                value_table(&keep_18, map19);
            }
            TRY(run_map_put(r, pos, map12, S("keep"), &keep_18));
            Value timeout_ms_26;
            value_int(&timeout_ms_26, (int64_t)((hosted_run_args.spec).timeout_ms));
            TRY(run_map_put(r, pos, map12, S("timeout_ms"), &timeout_ms_26));
            Value want_27;
            TRY(hosted_put_str(r, pos, &((hosted_run_args.spec).want), &want_27));
            TRY(run_map_put(r, pos, map12, S("want"), &want_27));
            value_table(&spec_11, map12);
        }
        TRY(run_map_put(r, pos, q, S("spec"), &spec_11));
        return OK;
    }
    case HOSTED_WRITE: {
        Value path_28;
        TRY(hosted_put_str(r, pos, &(hosted_write_args.path), &path_28));
        TRY(run_map_put(r, pos, q, S("path"), &path_28));
        Value text_29;
        TRY(hosted_put_str(r, pos, &(hosted_write_args.text), &text_29));
        TRY(run_map_put(r, pos, q, S("text"), &text_29));
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
                Values items30;
                TRY(hosted_items(r, pos, v, &items30));
                void *elems31 = NULL;
                TRY(hosted_list(r, pos, items30.len, sizeof(RocStr), &(hosted_answer.glob), &elems31));
                RocStr *made32 = elems31;
                for (size_t k33 = 0; k33 < items30.len; k33++) {
                    TRY(hosted_str(r, pos, &at(items30, k33), &(made32[k33])));
                }
            }
        }
        return OK;
    case HOSTED_GREP_ALL:
        {
            {
                Values items34;
                TRY(hosted_items(r, pos, v, &items34));
                void *elems35 = NULL;
                TRY(hosted_list(r, pos, items34.len, sizeof(AnonStruct1d63bdf4a74e5a0f), &(hosted_answer.grep_all), &elems35));
                AnonStruct1d63bdf4a74e5a0f *made36 = elems35;
                for (size_t k37 = 0; k37 < items34.len; k37++) {
                    {
                        Value line_38;
                        TRY(hosted_get(r, pos, &at(items34, k37), S("line"), &line_38));
                        {
                            int64_t n39 = 0;
                            TRY(hosted_int(r, pos, &line_38, &n39));
                            (made36[k37]).line = (uint64_t)n39;
                        }
                    }
                    {
                        Value text_40;
                        TRY(hosted_get(r, pos, &at(items34, k37), S("text"), &text_40));
                        TRY(hosted_str(r, pos, &text_40, &((made36[k37]).text)));
                    }
                }
            }
        }
        return OK;
    case HOSTED_LIST:
        {
            {
                Value tag41;
                TRY(hosted_get(r, pos, v, S("tag"), &tag41));
                if (tag41.tag != VALUE_STRING) return hosted_wrong(r, pos, S("tag"));
                if (str_eq(tag41.as.string, S("Found"))) {
                    Value value42;
                    TRY(hosted_get(r, pos, v, S("value"), &value42));
                    RocList payload43;
                    memset(&payload43, 0, sizeof payload43);
                    {
                        Values items44;
                        TRY(hosted_items(r, pos, &value42, &items44));
                        void *elems45 = NULL;
                        TRY(hosted_list(r, pos, items44.len, sizeof(RocStr), &(payload43), &elems45));
                        RocStr *made46 = elems45;
                        for (size_t k47 = 0; k47 < items44.len; k47++) {
                            TRY(hosted_str(r, pos, &at(items44, k47), &(made46[k47])));
                        }
                    }
                    hosted_answer.list = FoundOrMissingType7_make_found(payload43);
                } else if (str_eq(tag41.as.string, S("Missing"))) {
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
    case HOSTED_READ:
        {
            {
                Value tag48;
                TRY(hosted_get(r, pos, v, S("tag"), &tag48));
                if (tag48.tag != VALUE_STRING) return hosted_wrong(r, pos, S("tag"));
                if (str_eq(tag48.as.string, S("Found"))) {
                    Value value49;
                    TRY(hosted_get(r, pos, v, S("value"), &value49));
                    RocStr payload50;
                    memset(&payload50, 0, sizeof payload50);
                    TRY(hosted_str(r, pos, &value49, &(payload50)));
                    hosted_answer.read = FoundOrMissingType8_make_found(payload50);
                } else if (str_eq(tag48.as.string, S("Missing"))) {
                    hosted_answer.read = FoundOrMissingType8_make_missing();
                } else {
                    return hosted_wrong(r, pos, S("tag"));
                }
            }
        }
        return OK;
    case HOSTED_RUN:
        {
            {
                Value code_51;
                TRY(hosted_get(r, pos, v, S("code"), &code_51));
                {
                    int64_t n52 = 0;
                    TRY(hosted_int(r, pos, &code_51, &n52));
                    (hosted_answer.run).code = (int64_t)n52;
                }
            }
            {
                Value out_53;
                TRY(hosted_get(r, pos, v, S("out"), &out_53));
                TRY(hosted_str(r, pos, &out_53, &((hosted_answer.run).out)));
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
