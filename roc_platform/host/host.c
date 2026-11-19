// host.c: the host of Shale's Roc platform (roc_platform/README.md).
//
// **C SHALE IS THE HOST, AS A LIBRARY**: this file includes c/shale.c whole,
// so the command line, the mounts, the preludes, the budgets, the report and
// the exit code are `shale`'s own. What it runs is not a script file but the
// driver (HOSTED_DRIVER, in hosted.gen.c): a loop of Shale that asks the Roc
// app for its next effect (`host("roc_step", answer)`), does it with Shale's
// own builtin, and hands the answer back. So every effect a Roc app makes is
// a Shale builtin's call, under Shale's refusals and budgets, and a refusal
// is a Shale stop with Shale's words.
//
// **THE HOSTED FUNCTIONS ARE GENERATED** from c/builtins.txt, the one home of
// Shale's builtins, by c/gen_builtins.shale: hosted.gen.c holds each one's
// driver clause, its shim (`shale_<name>`, the names roc_platform_abi.h
// declares), its request as a Shale map and its answer as Roc's value. This
// file holds what they share: the Roc's stack and memory, Roc's strings and
// lists, and `roc_step`.
//
// **THE ROC RUNS ON A STACK OF ITS OWN** (a ucontext): a shim writes its
// arguments, switches to Shale's stack, and returns the answer when Shale
// switches back. Nothing runs at once: one side waits while the other runs,
// so the arenas need no lock.
//
// **ROC'S MEMORY IS AN ARENA OF SHALE'S** (c/foundation/arena.h), under the
// run's `arena_bytes` budget: `roc_dealloc` gives nothing back, and a refusal
// stops the run as `budget arena`. `roc_crashed` stops it too, with Roc's
// message.
//
// Two names are taken over before c/shale.c is read: its `main`, which
// becomes `shale_cli_main`, and its `host_call`, the Linux host's dispatch,
// which becomes `shale_host_call` behind ours. Both are function-like
// macros, so only a name followed by `(` (a definition, a call) is renamed:
// run_script's `{&lh, host_call}` still names ours, and a struct's `.main`
// member is left alone.

#define _GNU_SOURCE

#include "script/run.h"

#define host_call(...) shale_host_call(__VA_ARGS__)
#define main(...) shale_cli_main(__VA_ARGS__)
// The dispatch run_script hands the VM: ours, defined below. The name is in
// parentheses so the macro above leaves it be.
static STATUS_MUST_USE Status(host_call)(void *ctx, Run *r, Pos pos, Str name, ConstValues args, Value *out);
#include "shale.c"
#undef main
#undef host_call

#include <sys/mman.h>
#include <ucontext.h>

#include "roc_platform_abi.h"

// ── the request ─────────────────────────────────────────────────────────

// What the Roc asked: a hosted function's kind (hosted.gen.c's `Hosted`,
// its arguments in that function's `hosted_<name>_args`), or one of the
// host's own, after them.
enum {
    // The app returned.
    REQ_DONE = 1000,
    // The Roc crashed (`crash`, a failed `expect`): `req_why` says why.
    REQ_CRASH,
    // The Roc's arena refused.
    REQ_ARENA,
};

static int req_kind;
static Str req_why;
static ucontext_t shale_side;
static ucontext_t roc_side;
static bool roc_started;
static Arena roc_arena;
static bool roc_refused;

// The Roc's stack: 64 MiB reserved, taken as it is touched, a page below it
// that faults rather than let an overflow write past it.
enum { ROC_STACK = 64 << 20, ROC_GUARD = 4096, ROC_HEADER = 16 };

// ── the Roc's memory ────────────────────────────────────────────────────

// `len` bytes of the Roc's arena, after a header holding `len` (roc_realloc
// reads it), aligned to 16: NULL when the arena's budget refuses.
static void *roc_bytes_or_null(size_t len) {
    char *p = arena_alloc(&roc_arena, ROC_HEADER + len, ROC_HEADER);
    if (p == NULL) return NULL;
    memcpy(p, &len, sizeof len);
    return p + ROC_HEADER;
}

// Back to Shale with a request it ends the run on; never resumed.
static void roc_stop_with(int kind, Str why) {
    req_kind = kind;
    req_why = why;
    swapcontext(&roc_side, &shale_side);
    c_bug("roc platform: the Roc resumed after its run ended");
}

// On the Roc's side: `len` bytes, or the run stopped on the arena's budget.
static void *roc_side_bytes(size_t len) {
    void *p = roc_bytes_or_null(len);
    if (p == NULL) roc_stop_with(REQ_ARENA, S(""));
    return p;
}

void *roc_alloc(size_t length, size_t alignment) {
    if (alignment > ROC_HEADER) c_bug("roc_alloc: an alignment past 16");
    return roc_side_bytes(length);
}

void *roc_realloc(void *ptr, size_t new_length, size_t alignment) {
    if (alignment > ROC_HEADER) c_bug("roc_realloc: an alignment past 16");
    size_t was = 0;
    memcpy(&was, (char *)ptr - ROC_HEADER, sizeof was);
    void *p = roc_side_bytes(new_length);
    memcpy(p, ptr, was < new_length ? was : new_length);
    return p;
}

void roc_dealloc(void *ptr, size_t alignment) {
    (void)ptr;
    (void)alignment;
}

// A copy of `s` in the Roc's arena, which outlives the request.
static Str roc_side_copy(Str s) {
    if (s.len == 0) return S("");
    char *p = roc_side_bytes(s.len);
    memcpy(p, s.ptr, s.len);
    return (Str){p, s.len};
}

void roc_crashed(const uint8_t *bytes, size_t len) {
    roc_stop_with(REQ_CRASH, roc_side_copy((Str){(const char *)bytes, len}));
}

void roc_expect_failed(const uint8_t *bytes, size_t len) {
    roc_stop_with(REQ_CRASH, roc_side_copy((Str){(const char *)bytes, len}));
}

void roc_dbg(const uint8_t *bytes, size_t len) {
    size_t n = fwrite(bytes, 1, len, stderr);
    (void)n;
    fputc('\n', stderr);
}

// ── Roc's strings and lists ─────────────────────────────────────────────

// A Roc Str's bytes: a small one's are inline, its length in the last
// byte's low seven bits, the high bit set.
static Str roc_str_view(const RocStr *s) {
    const uint8_t *raw = (const uint8_t *)s;
    uint8_t last = raw[sizeof(RocStr) - 1];
    if (last & 0x80) return (Str){(const char *)raw, (size_t)(last & 0x7f)};
    return (Str){(const char *)s->bytes, s->length};
}

// A Roc Str of `s`: a small one under 24 bytes, else a big one in the
// Roc's arena, its refcount (one) before the bytes and its capacity shifted
// left by one, as Roc's str.zig lays it. False when the arena refuses.
static bool roc_str_make(Str s, RocStr *out) {
    memset(out, 0, sizeof *out);
    uint8_t *raw = (uint8_t *)out;
    if (s.len < sizeof(RocStr)) {
        if (s.len > 0) memcpy(raw, s.ptr, s.len);
        raw[sizeof(RocStr) - 1] = (uint8_t)(0x80 | s.len);
        return true;
    }
    char *base = roc_bytes_or_null(sizeof(size_t) + s.len);
    if (base == NULL) return false;
    size_t one = 1;
    memcpy(base, &one, sizeof one);
    memcpy(base + sizeof(size_t), s.ptr, s.len);
    out->bytes = (uint8_t *)base + sizeof(size_t);
    out->length = s.len;
    out->capacity_or_alloc_ptr = s.len << 1;
    return true;
}

// A Roc list of `n` elements of `size` bytes whose elements hold refcounted
// values (two words before them, the count then the refcount, as Roc's
// utils.zig lays it): `*elems` is where they go. False when the arena
// refuses.
static bool roc_list_make(size_t n, size_t size, RocList *out, void **elems) {
    memset(out, 0, sizeof *out);
    *elems = NULL;
    if (n == 0) return true;
    size_t w = sizeof(size_t);
    char *base = roc_bytes_or_null(2 * w + n * size);
    if (base == NULL) return false;
    size_t one = 1;
    memcpy(base, &n, w);
    memcpy(base + w, &one, w);
    *elems = base + 2 * w;
    out->elements = *elems;
    out->length = n;
    out->capacity_or_alloc_ptr = n << 1;
    return true;
}

// ── what hosted.gen.c's code calls ──────────────────────────────────────

// On the Roc's side: hands the request to Shale and waits for its answer.
static void roc_ask(int kind) {
    req_kind = kind;
    swapcontext(&roc_side, &shale_side);
}

static STATUS_MUST_USE Status roc_refused_stop(Run *r, Pos pos) {
    return run_budget_stop(&r->ctx, pos, BUDGET_ARENA);
}

static STATUS_MUST_USE Status hosted_wrong(Run *r, Pos pos, Str what) {
    return run_failf(r, pos, CLASS_HOST, "the Roc platform's driver answered without {s}", S("the Roc platform's driver answered without "), what);
}

// A Roc Str as a Shale string, its bytes copied to the Roc's arena (a small
// one's live in the shim's arguments, which its return ends).
static STATUS_MUST_USE Status hosted_put_str(Run *r, Pos pos, const RocStr *s, Value *out) {
    Str v = roc_str_view(s);
    if (v.len == 0) {
        value_string(out, S(""));
        return OK;
    }
    char *p = roc_bytes_or_null(v.len);
    if (p == NULL) return roc_refused_stop(r, pos);
    memcpy(p, v.ptr, v.len);
    value_string(out, (Str){p, v.len});
    return OK;
}

// A new Shale list, to hold `n` items: a list of none is Shale's `{}`.
static STATUS_MUST_USE Status hosted_new_list(Run *r, Pos pos, size_t n, Table **out) {
    TRY(run_new_table(r, pos, out));
    // run_new_table's table is `{}`: its kind is set by what fills it, as
    // the VM's OP_MAP_BEGIN and OP_LIST_BEGIN set it.
    if (n > 0) (*out)->kind = TABLE_LIST;
    return OK;
}

static STATUS_MUST_USE Status hosted_new_map(Run *r, Pos pos, Table **out) {
    TRY(run_new_table(r, pos, out));
    (*out)->kind = TABLE_MAP;
    return OK;
}

// The field `key` of the answer map `v`.
static STATUS_MUST_USE Status hosted_get(Run *r, Pos pos, const Value *v, Str key, Value *out) {
    if (v->tag != VALUE_TABLE || v->as.table->kind != TABLE_MAP || !ValueMap_get(&v->as.table->map, key, out)) return hosted_wrong(r, pos, key);
    return OK;
}

static STATUS_MUST_USE Status hosted_str(Run *r, Pos pos, const Value *v, RocStr *out) {
    if (v->tag != VALUE_STRING) return hosted_wrong(r, pos, S("a string"));
    if (!roc_str_make(v->as.string, out)) return roc_refused_stop(r, pos);
    return OK;
}

static STATUS_MUST_USE Status hosted_int(Run *r, Pos pos, const Value *v, int64_t *out) {
    if (v->tag != VALUE_INT) return hosted_wrong(r, pos, S("an integer"));
    *out = v->as.integer;
    return OK;
}

static STATUS_MUST_USE Status hosted_bool(Run *r, Pos pos, const Value *v, bool *out) {
    if (v->tag != VALUE_BOOL) return hosted_wrong(r, pos, S("a boolean"));
    *out = v->as.boolean;
    return OK;
}

// A Shale list's items (an empty table is the empty list).
static STATUS_MUST_USE Status hosted_items(Run *r, Pos pos, const Value *v, Values *items) {
    items->ptr = NULL;
    items->len = 0;
    if (v->tag != VALUE_TABLE) return hosted_wrong(r, pos, S("a list"));
    if (v->as.table->kind == TABLE_EMPTY) return OK;
    if (v->as.table->kind != TABLE_LIST) return hosted_wrong(r, pos, S("a list"));
    items->ptr = v->as.table->list.ptr;
    items->len = v->as.table->list.len;
    return OK;
}

// A Roc list of `n` items of `size` bytes, or the run stopped on the arena.
static STATUS_MUST_USE Status hosted_list(Run *r, Pos pos, size_t n, size_t size, RocList *out, void **elems) {
    if (!roc_list_make(n, size, out, elems)) return roc_refused_stop(r, pos);
    return OK;
}

#include "hosted.gen.c"

// The app's whole run, on its own stack: then `done`.
static void roc_entry(void) {
    roc_main();
    roc_stop_with(REQ_DONE, S(""));
}

// ── Shale's side: `roc_step` ────────────────────────────────────────────

// The request as a map: `fn` names it, and its arguments are its fields.
static STATUS_MUST_USE Status req_value(Run *r, Pos pos, Value *out) {
    Table *t = NULL;
    TRY(hosted_new_map(r, pos, &t));
    if (req_kind == REQ_DONE) {
        TRY(run_map_put_str(r, pos, t, S("fn"), S("done")));
    } else if (req_kind == REQ_CRASH) {
        TRY(run_map_put_str(r, pos, t, S("fn"), S("crash")));
        TRY(run_map_put_str(r, pos, t, S("why"), req_why));
    } else {
        if (req_kind < 0 || req_kind >= HOSTED_COUNT) c_bug("roc platform: a request of no kind");
        TRY(run_map_put_str(r, pos, t, S("fn"), hosted_names[req_kind]));
        TRY(hosted_request(r, pos, req_kind, t));
    }
    value_table(out, t);
    return OK;
}

// `host("roc_step"[, answer])`: the answer handed to the Roc (none at the
// first call, which starts it), and the Roc run to its next request, given
// back as a map.
static STATUS_MUST_USE Status roc_step(Run *r, Pos pos, ConstValues args, Value *out) {
    if (!roc_started) {
        if (args.len != 0) return run_failf(r, pos, CLASS_ARGUMENT, "`roc_step`'s first call takes no answer", S("`roc_step`'s first call takes no answer"));
        arena_init(&roc_arena, r->ctx.budgets.arena_bytes, &roc_refused);
        char *stack = mmap(NULL, ROC_STACK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0);
        if (stack == MAP_FAILED) return run_failf(r, pos, CLASS_HOST, "the Roc platform: no stack for the Roc", S("the Roc platform: no stack for the Roc"));
        if (mprotect(stack, ROC_GUARD, PROT_NONE) != 0) c_bug("roc platform: the stack's guard page");
        if (getcontext(&roc_side) != 0) c_bug("roc platform: getcontext");
        roc_side.uc_stack.ss_sp = stack;
        roc_side.uc_stack.ss_size = ROC_STACK;
        roc_side.uc_link = NULL;
        makecontext(&roc_side, roc_entry, 0);
        roc_started = true;
    } else {
        if (args.len != 1) return run_failf(r, pos, CLASS_ARGUMENT, "`roc_step` takes the last request's answer", S("`roc_step` takes the last request's answer"));
        if (req_kind < 0 || req_kind >= HOSTED_COUNT) return run_failf(r, pos, CLASS_HOST, "the Roc platform: `roc_step` after the app ended", S("the Roc platform: `roc_step` after the app ended"));
        TRY(hosted_answer_from(r, pos, req_kind, &at(args, 0)));
    }
    swapcontext(&shale_side, &roc_side);
    if (req_kind == REQ_ARENA) return roc_refused_stop(r, pos);
    return req_value(r, pos, out);
}

static STATUS_MUST_USE Status(host_call)(void *ctx, Run *r, Pos pos, Str name, ConstValues args, Value *out) {
    if (str_eq(name, S("roc_step"))) return roc_step(r, pos, args, out);
    return shale_host_call(ctx, r, pos, name, args, out);
}

// `<program> [--budget NAME=N]... [NAME=VALUE]...`: `shale -e HOSTED_DRIVER`
// with the rest of the command line, in the folder it runs in.
int main(int argc, char **argv) {
    char **all = calloc((size_t)argc + 3, sizeof *all);
    if (all == NULL) return 2;
    all[0] = argv[0];
    int n = 1;
    // The `--budget`s come before the script, the values after it.
    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "--budget") == 0 && k + 1 < argc) {
            all[n++] = argv[k];
            all[n++] = argv[++k];
        }
    }
    all[n++] = "-e";
    all[n++] = (char *)HOSTED_DRIVER;
    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "--budget") == 0 && k + 1 < argc) {
            k++;
            continue;
        }
        all[n++] = argv[k];
    }
    all[n] = NULL;
    return shale_cli_main(n, all);
}
