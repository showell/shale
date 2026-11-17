// test_parse.c: the parser against Zig Shale's parser's rules: what a source
// parses to, where each stop is and what it says, how the preludes load and
// are checked, and what a `use` binds. The `shale --check` command's own
// texts are judged by tools/differ.shale against Zig Shale.

#include "test.h"

#include "../script/cli.h"

// A core prelude of two functions, the way the real one is laid out.
static Str core(void) {
    return S("function twice(s)\n  return s .. s\nend\n\nfunction opt(a, b?)\n  return a\nend\n");
}

typedef struct {
    Arena arena;
    Parser p;
    Program prog;
    Status status;
} Parsed;

typedef struct {
    Str path;
    Str source;
    int loads;
} FakeLibrary;

static STATUS_MUST_USE Status fake_load(void *ctx, Arena *into, Str path, Str *why, Str *out, bool *got) {
    (void)into;
    FakeLibrary *f = ctx;
    f->loads++;
    *got = str_eq(path, f->path);
    if (*got) {
        *out = f->source;
    } else {
        *why = S("not found");
    }
    return OK;
}

static void parse_with(Parsed *r, Str source, Preludes extra, FakeLibrary *lib) {
    r->arena = arena_make(1 << 24);
    parse_init(&r->p, &r->arena, source, 200);
    ParseLoader none = {NULL, NULL};
    ParseLoader loader = {lib, fake_load};
    r->status = parse_program(&r->p, core(), extra, lib != NULL ? loader : none, &r->prog);
}

static void parse_plain(Parsed *r, Str source) {
    Preludes none = {NULL, 0};
    parse_with(r, source, none, NULL);
}

// The parse must have stopped, at `line`:`column`, with `cls` and `message`.
static void expect_stop(Parsed *r, uint32_t line, uint32_t column, StopClass cls, Str message) {
    EXPECT(r->status == STOP);
    EXPECT(r->p.stopped);
    EXPECT_EQ(r->p.stop.line, line);
    EXPECT_EQ(r->p.stop.column, column);
    EXPECT_EQ(r->p.stop.cls, cls);
    EXPECT_STR(r->p.stop.message, message);
}

TEST(a_script_parses_to_its_tree_each_name_a_slot) {
    Parsed r;
    parse_plain(&r, S("local a = 1\nlocal b = a + 2 * 3\nfunction f(x) return x end\nprint(f(b))\n"));
    EXPECT(r.status == OK);
    EXPECT_EQ(r.prog.main.frame, 2);
    EXPECT_EQ(r.prog.functions.len, 1);
    EXPECT_STR(at(r.prog.functions, 0).name, S("f"));
    EXPECT_EQ(at(r.prog.functions, 0).params, 1);
    Stmts body = r.prog.main.body;
    EXPECT_EQ(body.len, 4);
    EXPECT_EQ(at(body, 0).kind, STMT_LOCAL);
    EXPECT_EQ(at(body, 1).as.local.slot, 1);
    // `a + 2 * 3`: the product binds tighter.
    Expr *sum = at(body, 1).as.local.value;
    EXPECT_EQ(sum->kind, EXPR_BINARY);
    EXPECT_EQ(sum->as.binary.op, BINOP_ADD);
    EXPECT_EQ(sum->as.binary.left->kind, EXPR_LOCAL);
    EXPECT_EQ(sum->as.binary.right->as.binary.op, BINOP_MUL);
    // A function's declaration leaves an empty `do` where it stood.
    EXPECT_EQ(at(body, 2).kind, STMT_DO);
    EXPECT_EQ(at(body, 2).as.do_.len, 0);
    Expr *call = at(body, 3).as.call;
    EXPECT_EQ(call->as.call.target, TARGET_BUILTIN);
    EXPECT_EQ(call->as.call.builtin, BUILTIN_PRINT);
    EXPECT_EQ(at(call->as.call.args, 0)->as.call.target, TARGET_FUNCTION);
    arena_deinit(&r.arena);
}

TEST(a_minus_folds_into_its_literal_and_nil_comparisons_get_their_own_ops) {
    Parsed r;
    parse_plain(&r, S("local a = -9223372036854775808\nlocal b = -(5)\nlocal c = a == nil\nlocal d = nil ~= b\nlocal e = -a\n"));
    EXPECT(r.status == OK);
    Stmts body = r.prog.main.body;
    EXPECT(at(body, 0).as.local.value->as.integer == INT64_MIN);
    EXPECT_EQ(at(body, 1).as.local.value->kind, EXPR_INT);
    EXPECT(at(body, 1).as.local.value->as.integer == -5);
    EXPECT_EQ(at(body, 2).as.local.value->as.binary.op, BINOP_EQ_NIL);
    EXPECT_EQ(at(body, 3).as.local.value->as.binary.op, BINOP_NE_NIL);
    EXPECT_EQ(at(body, 4).as.local.value->kind, EXPR_UNARY);
    arena_deinit(&r.arena);
    parse_plain(&r, S("local x = 9223372036854775808\n"));
    expect_stop(&r, 1, 11, CLASS_SYNTAX, S("an integer literal too big for 64 bits (only -9223372036854775808, with its minus, is the smallest integer)"));
    arena_deinit(&r.arena);
}

TEST(a_core_prelude_function_is_parsed_at_its_first_call_and_only_then) {
    Parsed r;
    parse_plain(&r, S("print(twice(\"a\"))\nprint(twice(\"b\"))\n"));
    EXPECT(r.status == OK);
    // `opt` is never called, so never parsed; `twice` is parsed once.
    EXPECT_EQ(r.prog.functions.len, 1);
    EXPECT_STR(at(r.prog.functions, 0).name, S("the prelude's twice"));
    EXPECT_EQ(at(r.prog.functions, 0).pos.line, 1);
    arena_deinit(&r.arena);
    // The script's own function by a prelude's name takes its calls from its
    // line on.
    parse_plain(&r, S("print(twice(\"a\"))\nfunction twice(s) return s end\nprint(twice(\"b\"))\n"));
    EXPECT(r.status == OK);
    EXPECT_EQ(r.prog.functions.len, 2);
    EXPECT_STR(at(r.prog.functions, 1).name, S("twice"));
    arena_deinit(&r.arena);
}

TEST(a_call_is_held_to_its_functions_parameters) {
    Parsed r;
    parse_plain(&r, S("print(opt(1))\nprint(opt(1, 2))\n"));
    EXPECT(r.status == OK);
    // An optional parameter left out is passed nil.
    Expr *first = at(at(r.prog.main.body, 0).as.call->as.call.args, 0);
    EXPECT_EQ(first->as.call.args.len, 2);
    EXPECT_EQ(at(first->as.call.args, 1)->kind, EXPR_NIL);
    arena_deinit(&r.arena);
    parse_plain(&r, S("print(opt())\n"));
    expect_stop(&r, 1, 7, CLASS_ARGUMENT, S("`opt` takes 1 to 2 arguments, given 0"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("function f(a) return a end\nf(1, 2)\n"));
    expect_stop(&r, 2, 1, CLASS_ARGUMENT, S("`f` takes 1 argument, given 2"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("function f(a?, b) return a end\n"));
    expect_stop(&r, 1, 16, CLASS_SYNTAX, S("`b` is required after an optional parameter: the optional ones (`a?`) come last"));
    arena_deinit(&r.arena);
}

TEST(names_are_resolved_where_they_stand) {
    Parsed r;
    parse_plain(&r, S("print(f(1))\nfunction f(x) return x end\n"));
    expect_stop(&r, 1, 7, CLASS_NAME, S("no function `f` above this line: a function is called only after it is declared"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("print(nope + 1)\n"));
    expect_stop(&r, 1, 7, CLASS_NAME, S("no local `nope`: declare it with `local nope = ...` (there are no globals; Shale's builtins and preludes: `shale --help`)"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("local y = 1\nfunction g() return y end\n"));
    expect_stop(&r, 2, 21, CLASS_NAME, S("`y` is a script `local` set once, and a function sees only its own parameters and locals and the `const`s above it: declare it `const y = ...` where it is set"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("local y = 1\ny = 2\nfunction g() return y end\n"));
    expect_stop(&r, 3, 21, CLASS_NAME, S("`y` is a script `local` that is assigned again, and a function sees only its own parameters and locals: pass `y` in as a parameter"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("const K = 1\nK = 2\n"));
    expect_stop(&r, 2, 1, CLASS_NAME, S("a `const` is never assigned: declare a `local` to change it"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("local x = ipairs\n"));
    expect_stop(&r, 1, 11, CLASS_REFUSED, S("`ipairs` is not in this language: `for x in list do ... end`, or `for i = 1, #list do`"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("local x = print\n"));
    expect_stop(&r, 1, 11, CLASS_REFUSED, S("`print` is a builtin, not a value: call it, `print(...)`"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("local a = 1\nlocal a = 2\n"));
    expect_stop(&r, 2, 1, CLASS_NAME, S("`a` is already a local in this block"));
    arena_deinit(&r.arena);
}

TEST(a_syntax_error_right_after_a_long_string_names_its_closer) {
    Parsed r;
    parse_plain(&r, S("local s = [=[a]=] b]=]\n"));
    expect_stop(&r, 1, 19, CLASS_NAME, S("no local `b`: declare it with `local b = ...` (there are no globals; Shale's builtins and preludes: `shale --help`)"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("print([=[a]=] ]=])\n"));
    expect_stop(&r, 1, 15, CLASS_SYNTAX, S("expected `)` to close the call, found `]` (the long string at 1:7 closed at 1:11; if it holds `]=]`, open it with `[==[`)"));
    arena_deinit(&r.arena);
}

TEST(the_nesting_budget_stops_a_deep_source_where_it_is_passed) {
    Arena a = arena_make(1 << 20);
    Buf b = buf_make(&a);
    EXPECT(buf_lit(&b, "print(") == OK);
    for (int i = 0; i < 300; i++) EXPECT(buf_byte(&b, '(') == OK);
    EXPECT(buf_lit(&b, "1") == OK);
    Parsed r;
    parse_plain(&r, buf_view(&b));
    EXPECT(r.status == STOP);
    EXPECT_EQ(r.p.stop.cls, CLASS_BUDGET);
    EXPECT_EQ(r.p.stop.budget, BUDGET_NESTING);
    EXPECT_STR(r.p.stop.message, S("the source nests deeper than the nesting budget"));
    EXPECT_EQ(r.p.stop.line, 1);
    arena_deinit(&r.arena);
    arena_deinit(&a);
}

TEST(table_literals_are_a_list_or_a_map_never_both_and_hold_no_nil) {
    Parsed r;
    parse_plain(&r, S("local m = {a = 1, [\"b c\"] = 2}\nlocal l = {1, 2; 3}\nlocal e = {}\n"));
    EXPECT(r.status == OK);
    Stmts body = r.prog.main.body;
    EXPECT_EQ(at(body, 0).as.local.value->kind, EXPR_MAP);
    EXPECT_STR(at(at(body, 0).as.local.value->as.map, 1).key, S("b c"));
    EXPECT_EQ(at(body, 1).as.local.value->as.list.len, 3);
    EXPECT_EQ(at(body, 2).as.local.value->kind, EXPR_EMPTY_TABLE);
    arena_deinit(&r.arena);
    parse_plain(&r, S("local m = {1, a = 2}\n"));
    expect_stop(&r, 1, 15, CLASS_TYPE, S("a table literal mixes positional and keyed entries: a list or a map, not both"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("local m = {a = 1, a = 2}\n"));
    expect_stop(&r, 1, 19, CLASS_INDEX, S("the key `a` is given twice"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("local l = {1, nil}\n"));
    expect_stop(&r, 1, 15, CLASS_TYPE, S("a list cannot hold nil"));
    arena_deinit(&r.arena);
}

TEST(arg_names_read_by_literal_are_kept_once_and_one_by_value_says_so) {
    Parsed r;
    parse_plain(&r, S("local a = arg(\"dir\", \".\")\nlocal b = arg(\"n\", 1)\nlocal c = arg(\"dir\", \"x\")\n"));
    EXPECT(r.status == OK);
    EXPECT(!r.prog.args_by_value);
    EXPECT_EQ(r.prog.args_read.len, 2);
    Arg given[2] = {{S("dir"), S("src")}, {S("cwd"), S("x")}};
    Stop s;
    bool found = false;
    EXPECT(cli_unread_arg(&r.arena, &r.prog, given, 2, &s, &found) == OK);
    EXPECT(found);
    EXPECT_STR(s.message, S("`cwd=` is given, and the script reads no `arg(\"cwd\")`: it reads dir, n"));
    EXPECT_EQ(s.cls, CLASS_ARGUMENT);
    EXPECT(cli_unread_arg(&r.arena, &r.prog, given, 1, &s, &found) == OK);
    EXPECT(!found);
    arena_deinit(&r.arena);
    parse_plain(&r, S("local k = \"dir\"\nprint(arg(k, 1))\n"));
    EXPECT(r.status == OK);
    EXPECT(r.prog.args_by_value);
    EXPECT(cli_unread_arg(&r.arena, &r.prog, given, 2, &s, &found) == OK);
    EXPECT(!found);
    arena_deinit(&r.arena);
}

TEST(a_later_prelude_is_checked_whole_before_the_script) {
    Prelude ok = {S("the user prelude"), S("function shout(s)\n  return twice(s)\nend\n")};
    Preludes one = {&ok, 1};
    Parsed r;
    parse_with(&r, S("print(shout(\"a\"))\n"), one, NULL);
    EXPECT(r.status == OK);
    // `shout`, then the core's `twice` it calls; the check's own parse is
    // thrown away and adds none.
    EXPECT_EQ(r.prog.functions.len, 2);
    EXPECT_STR(at(r.prog.functions, 0).name, S("the user prelude's shout"));
    EXPECT_STR(at(r.prog.functions, 1).name, S("the prelude's twice"));
    arena_deinit(&r.arena);
    Prelude again = {S("the user prelude"), S("function twice(s)\n  return s\nend\n")};
    Preludes clash = {&again, 1};
    parse_with(&r, S("print(1)\n"), clash, NULL);
    expect_stop(&r, 1, 1, CLASS_NAME, S("the user prelude's `twice` is the prelude's already: a later prelude may not redefine an earlier one's function"));
    arena_deinit(&r.arena);
    Prelude bad = {S("the user prelude"), S("function f()\n  return @\nend\n")};
    Preludes broken = {&bad, 1};
    parse_with(&r, S("print(1)\n"), broken, NULL);
    expect_stop(&r, 2, 10, CLASS_SYNTAX, S("the user prelude: a character no token starts with, `@`"));
    arena_deinit(&r.arena);
}

TEST(a_use_binds_its_consts_on_its_line_and_its_functions_load_at_their_call) {
    FakeLibrary lib = {S("cookbook:lib.shale"), S("const LIMIT = 3\nconst NAMES = {\"a\", \"b\"}\n\nfunction lim(x)\n  return twice(x)\nend\n"), 0};
    Preludes none = {NULL, 0};
    Parsed r;
    parse_with(&r, S("use(\"cookbook:lib.shale\")\nprint(LIMIT, lim(\"x\"))\n"), none, &lib);
    EXPECT(r.status == OK);
    EXPECT_EQ(lib.loads, 1);
    Stmt use = at(r.prog.main.body, 0);
    EXPECT_EQ(use.kind, STMT_DO);
    EXPECT_EQ(use.as.do_.len, 2);
    EXPECT_EQ(at(use.as.do_, 0).kind, STMT_CONSTANT);
    EXPECT_EQ(at(use.as.do_, 0).pos.line, 1);
    EXPECT_EQ(r.prog.const_names.len, 2);
    EXPECT_STR(at(r.prog.functions, 0).name, S("cookbook:lib.shale's lim"));
    arena_deinit(&r.arena);
    parse_with(&r, S("use(\"cookbook:gone.shale\")\n"), none, &lib);
    expect_stop(&r, 1, 1, CLASS_STORE, S("`use(\"cookbook:gone.shale\")`: not found"));
    arena_deinit(&r.arena);
    parse_with(&r, S("use(\"/abs/lib.shale\")\n"), none, &lib);
    expect_stop(&r, 1, 1, CLASS_ARGUMENT, S("`use(\"/abs/lib.shale\")`: a library is named through a mount, `name:path` (`cookbook:roc/pinned.shale`), never by an absolute path or the root's: a script runs from other repositories' roots"));
    arena_deinit(&r.arena);
    // An empty path, whose text may hold NULL, names no mount, and the
    // loader is never asked for it.
    FakeLibrary unasked = {S(""), S(""), 0};
    parse_with(&r, S("use(\"\")\n"), none, &unasked);
    expect_stop(&r, 1, 1, CLASS_ARGUMENT, S("`use(\"\")`: a library is named through a mount, `name:path` (`cookbook:roc/pinned.shale`), never by an absolute path or the root's: a script runs from other repositories' roots"));
    EXPECT_EQ(unasked.loads, 0);
    arena_deinit(&r.arena);
    parse_with(&r, S("function lim() return 1 end\nuse(\"cookbook:lib.shale\")\n"), none, &lib);
    expect_stop(&r, 2, 1, CLASS_NAME, S("`use(\"cookbook:lib.shale\")`: its function `lim` (cookbook:lib.shale:4) is the script's function at line 1 already"));
    arena_deinit(&r.arena);
    FakeLibrary runs = {S("cookbook:bad.shale"), S("const X = now()\n"), 0};
    parse_with(&r, S("use(\"cookbook:bad.shale\")\n"), none, &runs);
    expect_stop(&r, 1, 1, CLASS_REFUSED, S("`use(\"cookbook:bad.shale\")`: cookbook:bad.shale:1:1: a library's `const X` is a literal (a string, a number, a boolean, or a list or map of them): a library runs nothing and reads no launch value where it is used"));
    arena_deinit(&r.arena);
    // A host that loads no library refuses every `use`.
    parse_plain(&r, S("use(\"cookbook:lib.shale\")\n"));
    expect_stop(&r, 1, 1, CLASS_HOST, S("`use`: this host loads no library"));
    arena_deinit(&r.arena);
}

TEST(statements_hold_their_own_rules) {
    Parsed r;
    parse_plain(&r, S("while true do\n  break\nend\nbreak\n"));
    expect_stop(&r, 4, 1, CLASS_SYNTAX, S("`break` outside a loop"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("function f()\n  return 1\n  print(2)\nend\n"));
    expect_stop(&r, 3, 3, CLASS_SYNTAX, S("`return` must be the last statement of its block"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("if true then\n  print(1)\n"));
    expect_stop(&r, 3, 1, CLASS_SYNTAX, S("expected `end` to close the `if`, found the end of the script"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("end\n"));
    expect_stop(&r, 1, 1, CLASS_SYNTAX, S("`end` with nothing open to close"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("local function f() end\n"));
    expect_stop(&r, 1, 1, CLASS_REFUSED, S("`local function` is not in this language: functions are declared at the top level with `function name(...)`"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("for k, v in x do end\n"));
    expect_stop(&r, 1, 6, CLASS_REFUSED, S("several loop names are not in this language: `for k in keys(m) do ... m[k] ... end`"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("x\n"));
    expect_stop(&r, 1, 1, CLASS_NAME, S("no local `x`: declare it with `local x = ...` (there are no globals; Shale's builtins and preludes: `shale --help`)"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("local t = {}\nt.x\n"));
    expect_stop(&r, 2, 1, CLASS_SYNTAX, S("a statement must be a call or an assignment, found the end of the script"));
    arena_deinit(&r.arena);
    parse_plain(&r, S("print(attempt(nope, 1))\n"));
    expect_stop(&r, 1, 15, CLASS_NAME, S("`attempt` calls a function of the script declared above it, and there is no function `nope` above this line"));
    arena_deinit(&r.arena);
}

// The parse must have stopped with its state as it was before the script's
// first token: what Zig's `defer`s put back on every way out.
static void expect_state_restored(Parsed *r, Str source) {
    EXPECT(r->status == STOP);
    EXPECT(r->p.stopped);
    EXPECT_EQ(r->p.nesting, 0);
    EXPECT_EQ(r->p.frame.depth, 0);
    EXPECT_EQ(r->p.frame.loops, 0);
    EXPECT_EQ(r->p.frame.locals.len, 0);
    EXPECT(!r->p.in_function);
    EXPECT(r->p.script_frame == NULL);
    EXPECT(r->p.in_prelude == PARSE_SCRIPT);
    EXPECT(r->p.lexer.src.ptr == source.ptr);
    EXPECT_EQ(r->p.stmts.len, 0);
    EXPECT_EQ(r->p.exprs.len, 0);
    EXPECT_EQ(r->p.arms.len, 0);
    EXPECT_EQ(r->p.entries.len, 0);
}

TEST(a_stop_partway_leaves_the_parsers_state_as_it_was) {
    Parsed r;
    // Deep in a function's loop, `if`, call and table: every level put back.
    Str deep = S("local a = 1\nfunction f(x)\n  while x do\n    for i = 1, 3 do\n      if x then\n        print(1, {2, {k = nope}})\n      end\n    end\n  end\nend\n");
    parse_plain(&r, deep);
    expect_stop(&r, 6, 27, CLASS_NAME, S("no local `nope` here: a function sees only its own parameters and locals, and the script's `const`s declared above it (Shale's builtins and preludes: `shale --help`)"));
    expect_state_restored(&r, deep);
    arena_deinit(&r.arena);
    // A stop inside a prelude function loaded at its first call puts back
    // the script's lexer and token.
    Str calls = S("local s = \"a\"\nprint(broken(s))\n");
    r.arena = arena_make(1 << 24);
    parse_init(&r.p, &r.arena, calls, 200);
    Preludes none = {NULL, 0};
    ParseLoader no_loader = {NULL, NULL};
    r.status = parse_program(&r.p, S("function broken(s)\n  return s + @\nend\n"), none, no_loader, &r.prog);
    expect_stop(&r, 2, 14, CLASS_SYNTAX, S("a character no token starts with, `@`"));
    expect_state_restored(&r, calls);
    EXPECT_EQ(r.p.tok.line, 2);
    arena_deinit(&r.arena);
    // The nesting budget's own refusal counts no level it did not enter.
    Arena a = arena_make(1 << 20);
    Buf b = buf_make(&a);
    EXPECT(buf_lit(&b, "if true then\n  print(") == OK);
    for (int i = 0; i < 300; i++) EXPECT(buf_byte(&b, '(') == OK);
    EXPECT(buf_lit(&b, "1") == OK);
    Str nested = buf_view(&b);
    parse_plain(&r, nested);
    EXPECT_EQ(r.p.stop.budget, BUDGET_NESTING);
    expect_state_restored(&r, nested);
    arena_deinit(&r.arena);
    arena_deinit(&a);
    // A parse that does not stop ends where it began too.
    Str fine = S("local a = {1, {k = 2}}\nfor i = 1, 2 do if a then print(twice(\"x\")) end end\n");
    parse_plain(&r, fine);
    EXPECT(r.status == OK);
    EXPECT_EQ(r.p.nesting, 0);
    EXPECT_EQ(r.p.frame.depth, 0);
    EXPECT(r.p.lexer.src.ptr == fine.ptr);
    EXPECT_EQ(r.p.exprs.len, 0);
    arena_deinit(&r.arena);
}

TEST(the_command_line_and_the_header_set_budgets_the_command_line_winning) {
    Arena a = arena_make(1 << 20);
    Buf why = buf_make(&a);
    Command c;
    Str ok[5] = {S("--check"), S("--budget"), S("steps=99"), S("a.shale"), S("n=1")};
    EXPECT(cli_parse(ok, 5, false, S(""), &c, &why) == OK);
    EXPECT(c.check);
    EXPECT_STR(c.file, S("a.shale"));
    EXPECT_EQ(c.budgets.steps, 99);
    EXPECT_EQ(c.budgets.output_bytes, 64 * 1024);
    EXPECT_EQ(c.args_len, 1);
    EXPECT(cli_header_budgets(S("-- A RECIPE\n-- budget: steps=900 reads=7\n\nprint(1)\n"), &c, &why) == OK);
    // The command line's value wins over the header's.
    EXPECT_EQ(c.budgets.steps, 99);
    EXPECT_EQ(c.budgets.reads, 7);
    Buf w = buf_make(&a);
    EXPECT(cli_write_header_budgets(&w, &c) == OK);
    EXPECT_STR(buf_view(&w), S("── budgets from the script's header: reads=7\n"));
    Str bad[3] = {S("--budget"), S("stepz=3"), S("a.shale")};
    EXPECT(cli_parse(bad, 3, false, S(""), &c, &why) == STOP);
    EXPECT_STR(buf_view(&why), S("--budget stepz=3: no budget is called stepz"));
    buf_clear(&why);
    Str two[2] = {S("a.shale"), S("b.shale")};
    EXPECT(cli_parse(two, 2, false, S(""), &c, &why) == STOP);
    EXPECT_STR(buf_view(&why), S("one script: b.shale is a second"));
    buf_clear(&why);
    Str most[2] = {S("--budget"), S("kept_bytes=268435457")};
    EXPECT(cli_parse(most, 2, false, S(""), &c, &why) == STOP);
    EXPECT_STR(buf_view(&why), S("--budget kept_bytes=268435457: kept_bytes is at most 268435456"));
    buf_clear(&why);
    EXPECT(cli_header_budgets(S("print(1)\n  --budget: steps=9\n"), &c, &why) == STOP);
    EXPECT_STR(buf_view(&why), S("a `-- budget:` line after the script's first line of code is never read: put it among the comments opening the script"));
    arena_deinit(&a);
}

TEST(the_parsers_own_lookups_end_the_process_on_a_miss) {
    // A local's slot the frame doesn't hold, and a prelude function its own
    // index lacks, are the parser's bugs, never a script's stop.
    Arena a = arena_make(1 << 20);
    Parser p;
    parse_init(&p, &a, S("local x = 1\n"), 200);
    EXPECT_DIES((void)parse_is_const(&p, 7));
    PreludeFunctions none = {NULL, 0};
    EXPECT_DIES((void)parse_index_sure(none, S("twice")));
    arena_deinit(&a);
}

TEST_MAIN(TEST_CASE(a_script_parses_to_its_tree_each_name_a_slot),
          TEST_CASE(a_minus_folds_into_its_literal_and_nil_comparisons_get_their_own_ops),
          TEST_CASE(a_core_prelude_function_is_parsed_at_its_first_call_and_only_then),
          TEST_CASE(a_call_is_held_to_its_functions_parameters),
          TEST_CASE(names_are_resolved_where_they_stand),
          TEST_CASE(a_syntax_error_right_after_a_long_string_names_its_closer),
          TEST_CASE(the_nesting_budget_stops_a_deep_source_where_it_is_passed),
          TEST_CASE(table_literals_are_a_list_or_a_map_never_both_and_hold_no_nil),
          TEST_CASE(arg_names_read_by_literal_are_kept_once_and_one_by_value_says_so),
          TEST_CASE(a_later_prelude_is_checked_whole_before_the_script),
          TEST_CASE(a_use_binds_its_consts_on_its_line_and_its_functions_load_at_their_call),
          TEST_CASE(statements_hold_their_own_rules),
          TEST_CASE(a_stop_partway_leaves_the_parsers_state_as_it_was),
          TEST_CASE(the_command_line_and_the_header_set_budgets_the_command_line_winning),
          TEST_CASE(the_parsers_own_lookups_end_the_process_on_a_miss))
