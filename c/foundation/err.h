// err.h: how a run stops. A function that can stop returns a `Status`
// (status.h); when it is STOP, the run's context says where and why: the
// stop's place, its class (`type`, `index`, `budget` with which budget), its
// message, the function it stopped in and the calls that led there, as Zig
// Shale's `diag.Stop` holds them (src/script/diag.zig).
//
// The names and the texts are Zig Shale's, byte for byte: the class and budget
// names are its enums' tag names, `run_budget_stop`'s messages are
// `interp.Run.budgetStop`'s, and `stop_report` writes `script.report`'s lines.

#ifndef SHALE_ERR_H
#define SHALE_ERR_H

#include "buf.h"

typedef enum {
    CLASS_SYNTAX,
    CLASS_REFUSED,
    CLASS_NAME,
    CLASS_TYPE,
    CLASS_INDEX,
    CLASS_ARITHMETIC,
    CLASS_ARGUMENT,
    CLASS_BUDGET,
    CLASS_STORE,
    CLASS_FAIL,
    CLASS_HOST,
    CLASS_COUNT,
} StopClass;

// Which budget ran out; BUDGET_NONE for a stop of any other class.
typedef enum {
    BUDGET_NONE,
    BUDGET_STEPS,
    BUDGET_ARENA,
    BUDGET_CALL_DEPTH,
    BUDGET_OUTPUT,
    BUDGET_READS,
    BUDGET_READ_BYTES,
    BUDGET_CHANGES,
    BUDGET_CHANGE_BYTES,
    BUDGET_SOURCE,
    BUDGET_NESTING,
    BUDGET_DEPTH,
    BUDGET_KEPT,
    BUDGET_MACHINE,
    BUDGET_PROGRAMS,
    BUDGET_CPU,
    BUDGET_COUNT,
} BudgetKind;

SLICE_TYPE(Names, const Str);

// A class's name as a stop prints it.
static inline Str stop_class_name(StopClass c) {
    static const Str names[CLASS_COUNT] = {
        S("syntax"), S("refused"), S("name"), S("type"), S("index"), S("arithmetic"),
        S("argument"), S("budget"), S("store"), S("fail"), S("host"),
    };
    Names n = {names, CLASS_COUNT};
    return at(n, (size_t)c);
}

// A budget's name as a stop prints it after `budget `; "" for none.
static inline Str budget_name(BudgetKind b) {
    static const Str names[BUDGET_COUNT] = {
        S(""), S("steps"), S("arena"), S("call_depth"), S("output"), S("reads"),
        S("read_bytes"), S("changes"), S("change_bytes"), S("source"), S("nesting"),
        S("depth"), S("kept"), S("machine"), S("programs"), S("cpu"),
    };
    Names n = {names, BUDGET_COUNT};
    return at(n, (size_t)b);
}

// Every limit on a run, with Zig Shale's defaults (`interp.Budgets`). The CLI
// sets some apart (output 64 KiB, call bytes 64 MiB); those are the CLI's.
typedef struct {
    uint64_t steps;
    size_t arena_bytes;
    uint32_t call_depth;
    uint32_t programs;
    size_t output_bytes;
    uint64_t reads;
    uint64_t read_bytes;
    uint64_t changes;
    uint64_t change_bytes;
    size_t source_bytes;
    uint32_t nesting;
    uint32_t depth;
    size_t kept_bytes;
    size_t machine_bytes;
    uint64_t call_bytes;
    uint64_t call_entries;
} Budgets;

enum {
    BUDGETS_KEPT_BYTES_MOST = 256 * 1024 * 1024,
    BUDGETS_MACHINE_BYTES_MOST = 1024 * 1024 * 1024,
};

static inline Budgets budgets_default(void) {
    Budgets b = {
        .steps = 5000000,
        .arena_bytes = 16 * 1024 * 1024,
        .call_depth = 64,
        .programs = 1000,
        .output_bytes = 256 * 1024,
        .reads = 10000,
        .read_bytes = 64 * 1024 * 1024,
        .changes = 1000,
        .change_bytes = 16 * 1024 * 1024,
        .source_bytes = 64 * 1024,
        .nesting = 200,
        .depth = 2000,
        .kept_bytes = 1024 * 1024,
        .machine_bytes = 16 * 1024 * 1024,
        .call_bytes = 1024 * 1024,
        .call_entries = 256,
    };
    return b;
}

typedef struct {
    uint32_t line;
    uint32_t column;
} Pos;

// One call in a trail: where it was made, and in which function ("" at the
// top level; a prelude's is "the prelude's f", its line the prelude's).
typedef struct {
    uint32_t line;
    uint32_t column;
    Str function;
} Call;

SLICE_TYPE(Calls, const Call);
VEC_DEFINE(CallVec, Call)

typedef struct {
    uint32_t line;
    uint32_t column;
    StopClass cls;
    BudgetKind budget;
    Str message;
    // The function it stopped in; "" at the top level.
    Str function;
    // Every call between the top level and `function`, outermost first.
    Calls trail;
} Stop;

// The run's context, as far as a stop needs it: where messages are made, the
// budgets they quote, the function and calls the run is in, and its stop.
typedef struct {
    // The values' arena, where a formatted message is made.
    Arena *values;
    Budgets budgets;
    // The function the run is in; "" at the top level.
    Str function;
    // The calls in progress, outermost first, pushed as each starts.
    CallVec calls;
    bool stopped;
    Stop stop;
} RunCtx;

static inline RunCtx run_ctx_make(Arena *values, Arena *machine, Budgets budgets) {
    RunCtx r;
    memset(&r, 0, sizeof r);
    r.values = values;
    r.budgets = budgets;
    r.function = S("");
    r.calls = CallVec_make(machine);
    return r;
}

// Stops the run at `pos`, with `message` (which the caller keeps alive: a
// literal, or made in the values' arena).
static inline Status run_fail(RunCtx *r, Pos pos, StopClass cls, Str message) {
    // DRAGON: use after free. The stop's trail is a view of the live call vec in the machine's arena, not a copy, and its message lives in the caller's arena. A push after the stop can move the calls (freeing a big buffer), a pop or `attempt`'s cut changes what the trail says, and a reset of either arena frees what the stop points at. Nothing in the C yet runs between a stop and its report; a port of `attempt` or yield must copy what it keeps.
    Calls trail = {r->calls.ptr, r->calls.len};
    Stop s = {pos.line, pos.column, cls, BUDGET_NONE, message, r->function, trail};
    r->stop = s;
    r->stopped = true;
    return STOP;
}

// Stops the run with the message `b` built, or `fallback` when building it was
// refused: Zig's `allocPrint(...) catch fallback`.
static inline Status run_fail_built(RunCtx *r, Pos pos, StopClass cls, const Buf *b, Status built, Str fallback) {
    return run_fail(r, pos, cls, built == OK ? buf_view(b) : fallback);
}

// The message of a budget's stop, made in the values' arena where it quotes a
// number, as `interp.Run.budgetStop` makes it.
static inline Str run_budget_message(RunCtx *r, BudgetKind k) {
    Buf b = buf_make(r->values);
    Status s = OK;
    const Budgets *g = &r->budgets;
    switch (k) {
    case BUDGET_STEPS:
        s = buf_lit(&b, "the step budget (");
        if (s == OK) s = buf_u64(&b, g->steps);
        if (s == OK) s = buf_lit(&b, " steps) ran out");
        return s == OK ? buf_view(&b) : S("the step budget ran out");
    case BUDGET_ARENA: return S("the arena budget ran out");
    case BUDGET_CALL_DEPTH:
        s = buf_lit(&b, "calls nest deeper than the call-depth budget (");
        if (s == OK) s = buf_u64(&b, g->call_depth);
        if (s == OK) s = buf_lit(&b, ")");
        return s == OK ? buf_view(&b) : S("calls nest deeper than the call-depth budget");
    case BUDGET_OUTPUT: return S("the output budget ran out");
    case BUDGET_READS: return S("the store-read budget (count) ran out");
    case BUDGET_READ_BYTES: return S("the store-read budget (bytes) ran out");
    case BUDGET_CHANGES: return S("the store-change budget (count) ran out");
    case BUDGET_CHANGE_BYTES: return S("the store-change budget (bytes) ran out");
    case BUDGET_SOURCE: return S("the source is larger than the source budget");
    case BUDGET_NESTING: return S("the source nests deeper than the nesting budget");
    case BUDGET_DEPTH: return S("calls and the expressions inside them nest deeper than the depth budget");
    case BUDGET_MACHINE:
        s = buf_lit(&b, "the machine's memory (the code, the frames, the operands) is past its budget (");
        if (s == OK) s = buf_u64(&b, g->machine_bytes);
        if (s == OK) s = buf_lit(&b, " bytes): raise it with `--budget machine_bytes=N` or the script's `-- budget: machine_bytes=N` line, up to ");
        if (s == OK) s = buf_u64(&b, BUDGETS_MACHINE_BYTES_MOST);
        return s == OK ? buf_view(&b) : S("the machine's memory is past its budget");
    case BUDGET_KEPT:
        s = buf_lit(&b, "what the run keeps at a `yield` is past the kept budget (");
        if (s == OK) s = buf_u64(&b, g->kept_bytes);
        if (s == OK) s = buf_lit(&b, " bytes): raise it with `--budget kept_bytes=N` or the script's `-- budget: kept_bytes=N` line, up to ");
        if (s == OK) s = buf_u64(&b, BUDGETS_KEPT_BYTES_MOST);
        return s == OK ? buf_view(&b) : S("what the run keeps at a `yield` is past the kept budget");
    case BUDGET_PROGRAMS:
        s = buf_u64(&b, g->programs);
        if (s == OK) s = buf_lit(&b, " programs a run, the `programs` budget: raise it with `--budget programs=N` or the script's `-- budget: programs=N` line");
        return s == OK ? buf_view(&b) : S("the run's programs are past their budget");
    case BUDGET_CPU: return S("a program's CPU time is past its budget");
    case BUDGET_NONE:
    case BUDGET_COUNT: break;
    }
    // A stop on no budget is a bug in the C.
    fprintf(stderr, "err: a budget stop names no budget\n");
    abort();
    // Never reached. TCC doesn't know `abort` ends the process, and refuses
    // a function that might return no value.
    return S("");
}

// Stops the run at `pos` on budget `k`, with Zig's message for it.
static inline Status run_budget_stop(RunCtx *r, Pos pos, BudgetKind k) {
    run_fail(r, pos, CLASS_BUDGET, run_budget_message(r, k));
    r->stop.budget = k;
    return STOP;
}

// An allocation the arena refused, read as the arena budget, as Zig reads
// every `OutOfMemory` of a run.
// DRAGON: the arena budget. A container's STOP means only that its arena refused, with no stop recorded; one passed up by TRY without coming through here ends the run with `stopped` false and a zeroed Stop, so the report would name no budget. Every caller of a container must call this at its boundary. Nothing checks that it does.
static inline Status run_oom(RunCtx *r, Pos pos) {
    return run_budget_stop(r, pos, BUDGET_ARENA);
}

// The place a stop names: "the prelude" for a line in the prelude, else the script's name.
static inline Str stop_place(Str function, Str name) {
    return str_starts_with(function, S("the prelude's ")) ? S("the prelude") : name;
}

static inline Status stop_write_where(Buf *w, Str place, uint32_t line, uint32_t column, Str function) {
    TRY(buf_str(w, place));
    TRY(buf_byte(w, ':'));
    TRY(buf_u64(w, line));
    TRY(buf_byte(w, ':'));
    TRY(buf_u64(w, column));
    if (function.len > 0) {
        TRY(buf_lit(w, " (in `"));
        TRY(buf_str(w, function));
        TRY(buf_lit(w, "`)"));
    }
    return OK;
}

// A stopped run's report, as `script.report` writes it after the effects:
// `── stopped at name:line:column (in `f`): class budget: message (N steps)`,
// then a `   called from ...` line for each call, innermost first.
static inline Status stop_report(Buf *w, Str name, const Stop *s, uint64_t steps) {
    TRY(buf_lit(w, "── stopped at "));
    TRY(stop_write_where(w, stop_place(s->function, name), s->line, s->column, s->function));
    TRY(buf_lit(w, ": "));
    TRY(buf_str(w, stop_class_name(s->cls)));
    if (s->budget != BUDGET_NONE) {
        TRY(buf_byte(w, ' '));
        TRY(buf_str(w, budget_name(s->budget)));
    }
    TRY(buf_lit(w, ": "));
    TRY(buf_str(w, s->message));
    TRY(buf_lit(w, " ("));
    TRY(buf_u64(w, steps));
    TRY(buf_lit(w, " steps)\n"));
    for (size_t k = s->trail.len; k > 0; k--) {
        const Call c = at(s->trail, k - 1);
        TRY(buf_lit(w, "   called from "));
        TRY(stop_write_where(w, stop_place(c.function, name), c.line, c.column, c.function));
        TRY(buf_byte(w, '\n'));
    }
    return OK;
}

#endif
