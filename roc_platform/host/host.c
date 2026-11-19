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
// Two of run_script's calls go through ours, defined below (err.h and cli.h,
// read above, keep the real ones): run_ended, which puts a stop made while
// the Roc waits at the Roc's own line, and cli_write_report, which names the
// Roc's script for it and says the Roc's CPU time on the `── used:` line.
static STATUS_MUST_USE Status roc_run_ended(RunCtx *r, Status s);
static STATUS_MUST_USE Status roc_write_report(Buf *w, Str name, Preludes preludes, bool verbose, Names effects, Names records, const Stop *s, RunUsed u, const Budgets *b, const StepsTop *steps);
#define run_ended(...) roc_run_ended(__VA_ARGS__)
#define cli_write_report(...) roc_write_report(__VA_ARGS__)
#include "shale.c"
#undef cli_write_report
#undef run_ended
#undef main
#undef host_call

#include <signal.h>
#include <sys/mman.h>
#include <time.h>
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
    // The Roc's CPU time is past its budget: the timer stopped it.
    REQ_CPU,
};

static int req_kind;
static Str req_why;
// Where in the Roc's code the request came from: the call of the hosted
// function (its return address, less one, so it is inside the call), the
// crash's, the allocation's, or where the CPU budget's timer found the Roc.
// 0 for nowhere known.
static uintptr_t req_site;
static ucontext_t shale_side;
static ucontext_t roc_side;
static bool roc_started;
static Arena roc_arena;
static bool roc_refused;

// The Roc's stack: 64 MiB reserved, taken as it is touched, a page below it
// that faults rather than let an overflow write past it.
enum { ROC_STACK = 64 << 20, ROC_GUARD = 4096, ROC_HEADER = 16 };
static char *roc_stack;

// ── the Roc's CPU budget ────────────────────────────────────────────────
//
// **THE ROC'S OWN COMPUTATION IS HELD TO A BUDGET OF CPU TIME**,
// `roc_cpu_ms` (`--budget roc_cpu_ms=N`, a script's `## budget:` line;
// ROC_CPU_MS unset). Steps count Shale's work, and the Roc's work is no
// Shale step, so it is timed instead: the thread's CPU clock is read each
// time the Roc starts and each time it hands Shale a request, and the sum is
// the Roc's. A loop that never calls Shale is stopped by a timer on that
// clock, armed while the Roc runs for what is left of the budget: its signal
// takes the Roc off its stack (setcontext to Shale's side), so the run stops
// as `budget cpu`, at the Roc's line where the timer found it.
//
// The timer never lands in the host's own C on the Roc's side (roc_alloc's
// arena, which may call malloc; roc_dbg's stdio): there it only marks the
// budget spent, and that function stops the run as it returns.

enum { ROC_CPU_MS = 5000 };
static uint64_t roc_cpu_budget_ms = ROC_CPU_MS;
static uint64_t roc_cpu_ns;
static timer_t roc_timer;
static volatile sig_atomic_t roc_timer_armed;
static volatile sig_atomic_t roc_in_host;
static volatile sig_atomic_t roc_cpu_spent;

static uint64_t roc_thread_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0) c_bug("roc platform: the thread's CPU clock");
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static void roc_timer_set(uint64_t ns) {
    struct itimerspec it;
    memset(&it, 0, sizeof it);
    it.it_value.tv_sec = (time_t)(ns / 1000000000u);
    it.it_value.tv_nsec = (long)(ns % 1000000000u);
    if (timer_settime(roc_timer, 0, &it, NULL) != 0) c_bug("roc platform: the CPU budget's timer");
}

// Back to Shale with a request it ends the run on; never resumed.
static void roc_stop_with(int kind, Str why) {
    req_kind = kind;
    req_why = why;
    swapcontext(&roc_side, &shale_side);
    c_bug("roc platform: the Roc resumed after its run ended");
}

static void roc_cpu_alarm(int sig, siginfo_t *info, void *context) {
    (void)sig;
    (void)info;
    char here = 0;
    bool on_roc = roc_stack != NULL && &here >= roc_stack && &here < roc_stack + ROC_STACK;
    if (!on_roc) {
        // Between the timer's arming and the switch to the Roc, or its
        // switch back and the disarming: again shortly, if still armed.
        if (roc_timer_armed) roc_timer_set(1000000);
        return;
    }
    if (roc_in_host) {
        roc_cpu_spent = 1;
        return;
    }
    const ucontext_t *uc = context;
    req_site = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    req_kind = REQ_CPU;
    // Shale's side, as roc_step left it: the Roc is never resumed.
    setcontext(&shale_side);
}

// The host's own C on the Roc's side, between roc_host_enter and
// roc_host_leave: the timer only marks the budget spent there, and the
// leave stops the run, at `site`.
static void roc_host_enter(void) {
    roc_in_host = 1;
}

static void roc_host_leave(uintptr_t site) {
    roc_in_host = 0;
    if (roc_cpu_spent) {
        req_site = site;
        roc_stop_with(REQ_CPU, S(""));
    }
}

static void roc_cpu_start(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = roc_cpu_alarm;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGVTALRM, &sa, NULL) != 0) c_bug("roc platform: the CPU budget's signal");
    struct sigevent ev;
    memset(&ev, 0, sizeof ev);
    ev.sigev_notify = SIGEV_SIGNAL;
    ev.sigev_signo = SIGVTALRM;
    if (timer_create(CLOCK_THREAD_CPUTIME_ID, &ev, &roc_timer) != 0) c_bug("roc platform: the CPU budget's timer");
}

// ── the Roc's memory ────────────────────────────────────────────────────

// `len` bytes of the Roc's arena, after a header holding `len` (roc_realloc
// reads it), aligned to 16: NULL when the arena's budget refuses.
static void *roc_bytes_or_null(size_t len) {
    char *p = arena_alloc(&roc_arena, ROC_HEADER + len, ROC_HEADER);
    if (p == NULL) return NULL;
    memcpy(p, &len, sizeof len);
    return p + ROC_HEADER;
}

// On the Roc's side: `len` bytes, or the run stopped on the arena's budget,
// at `site`.
static void *roc_side_bytes(size_t len, uintptr_t site) {
    roc_host_enter();
    void *p = roc_bytes_or_null(len);
    if (p == NULL) {
        req_site = site;
        roc_in_host = 0;
        roc_stop_with(REQ_ARENA, S(""));
    }
    roc_host_leave(site);
    return p;
}

// The address of a call in the Roc's code, from inside the function it
// called: its return address, less one, so it is inside the call.
#define ROC_CALLER() ((uintptr_t)__builtin_return_address(0) - 1)

void *roc_alloc(size_t length, size_t alignment) {
    if (alignment > ROC_HEADER) c_bug("roc_alloc: an alignment past 16");
    return roc_side_bytes(length, ROC_CALLER());
}

void *roc_realloc(void *ptr, size_t new_length, size_t alignment) {
    if (alignment > ROC_HEADER) c_bug("roc_realloc: an alignment past 16");
    size_t was = 0;
    memcpy(&was, (char *)ptr - ROC_HEADER, sizeof was);
    void *p = roc_side_bytes(new_length, ROC_CALLER());
    memcpy(p, ptr, was < new_length ? was : new_length);
    return p;
}

void roc_dealloc(void *ptr, size_t alignment) {
    (void)ptr;
    (void)alignment;
}

// A copy of `s` in the Roc's arena, which outlives the request.
static Str roc_side_copy(Str s, uintptr_t site) {
    if (s.len == 0) return S("");
    char *p = roc_side_bytes(s.len, site);
    memcpy(p, s.ptr, s.len);
    return (Str){p, s.len};
}

void roc_crashed(const uint8_t *bytes, size_t len) {
    uintptr_t site = ROC_CALLER();
    Str why = roc_side_copy((Str){(const char *)bytes, len}, site);
    req_site = site;
    roc_stop_with(REQ_CRASH, why);
}

void roc_expect_failed(const uint8_t *bytes, size_t len) {
    uintptr_t site = ROC_CALLER();
    Str why = roc_side_copy((Str){(const char *)bytes, len}, site);
    req_site = site;
    roc_stop_with(REQ_CRASH, why);
}

void roc_dbg(const uint8_t *bytes, size_t len) {
    roc_host_enter();
    size_t n = fwrite(bytes, 1, len, stderr);
    (void)n;
    fputc('\n', stderr);
    roc_host_leave(ROC_CALLER());
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
// `site` is the shim's return address, in the Roc's call of it.
static void roc_ask(int kind, void *site) {
    req_kind = kind;
    req_site = (uintptr_t)site - 1;
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
    req_site = 0;
    roc_stop_with(REQ_DONE, S(""));
}

// ── where in the Roc a stop is ──────────────────────────────────────────
//
// **A STOP MADE WHILE THE ROC WAITS NAMES THE ROC'S LINE**, as a Shale
// script's stop names its own: a hosted call Shale refused, a `fail!`, a
// budget run out answering it, a crash, the Roc's arena or CPU budget. The
// site (`req_site`) is an address in the Roc's code; the launcher hands the
// program a table of the script's lines (`--roc-sites FILE`, made from the
// pinned Roc's DWARF line table, `objdump --dwarf=decodedline`, when it
// links the program), and the address's line there is the stop's. Roc's
// line table gives the line; the column is the call's own text on that line
// (`Shale.read!`, `crash`), which the launcher finds in the script, else the
// line's first. A site that is not in the script's own lines (Roc's
// builtins, the platform) leaves the stop at the driver's line, as before.
//
// The table's lines, each a record:
//   script PATH           the script, as the stop names it
//   base HEX              roc_main's address in the table's terms
//   row HEX LINE          from this address on, the script's LINE (0: none)
//   call LINE COLUMN NAME a call's text in the script
//   line LINE COLUMN      the line's first column that is not a blank

typedef struct {
    uint64_t addr;
    uint32_t line;
    uint32_t order;
} RocRow;

typedef struct {
    uint32_t line;
    uint32_t column;
    char name[48];
} RocCall;

static struct {
    bool loaded;
    char script[4096];
    uint64_t base;
    RocRow *rows;
    size_t rows_len;
    RocCall *calls;
    size_t calls_len;
    // Indexed by line: its first column, 0 for no entry.
    uint32_t *first;
    size_t first_len;
} roc_sites;

// Whether a stop was put at the Roc's line, so the report names its script.
static bool roc_stop_named;

static int roc_row_order(const void *a, const void *b) {
    const RocRow *x = a;
    const RocRow *y = b;
    if (x->addr != y->addr) return x->addr < y->addr ? -1 : 1;
    return x->order < y->order ? -1 : (x->order > y->order ? 1 : 0);
}

// The table at `path`, read once at the start, before the run: the host's own
// file, not the script's, so no budget of the run's is spent on it.
static void roc_sites_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "roc platform: no sites table %s\n", path);
        exit(2);
    }
    char l[4200];
    size_t rows_cap = 0, calls_cap = 0;
    uint32_t order = 0;
    while (fgets(l, sizeof l, f) != NULL) {
        size_t n = strlen(l);
        if (n > 0 && l[n - 1] == '\n') l[--n] = 0;
        unsigned long long a = 0;
        unsigned line = 0, col = 0;
        char name[48];
        if (strncmp(l, "script ", 7) == 0) {
            snprintf(roc_sites.script, sizeof roc_sites.script, "%s", l + 7);
        } else if (sscanf(l, "base %llx", &a) == 1) {
            roc_sites.base = a;
        } else if (sscanf(l, "row %llx %u", &a, &line) == 2) {
            if (roc_sites.rows_len == rows_cap) {
                rows_cap = rows_cap == 0 ? 256 : rows_cap * 2;
                roc_sites.rows = realloc(roc_sites.rows, rows_cap * sizeof *roc_sites.rows);
                if (roc_sites.rows == NULL) c_bug("roc platform: no memory for the sites table");
            }
            roc_sites.rows[roc_sites.rows_len++] = (RocRow){a, line, order++};
        } else if (sscanf(l, "call %u %u %47s", &line, &col, name) == 3) {
            if (roc_sites.calls_len == calls_cap) {
                calls_cap = calls_cap == 0 ? 64 : calls_cap * 2;
                roc_sites.calls = realloc(roc_sites.calls, calls_cap * sizeof *roc_sites.calls);
                if (roc_sites.calls == NULL) c_bug("roc platform: no memory for the sites table");
            }
            RocCall c = {line, col, {0}};
            snprintf(c.name, sizeof c.name, "%s", name);
            roc_sites.calls[roc_sites.calls_len++] = c;
        } else if (sscanf(l, "line %u %u", &line, &col) == 2) {
            if (line >= roc_sites.first_len) {
                size_t len = (size_t)line + 1;
                roc_sites.first = realloc(roc_sites.first, len * sizeof *roc_sites.first);
                if (roc_sites.first == NULL) c_bug("roc platform: no memory for the sites table");
                for (size_t k = roc_sites.first_len; k < len; k++) roc_sites.first[k] = 0;
                roc_sites.first_len = len;
            }
            roc_sites.first[line] = col;
        } else {
            fprintf(stderr, "roc platform: a sites table line it does not read: %s\n", l);
            exit(2);
        }
    }
    fclose(f);
    qsort(roc_sites.rows, roc_sites.rows_len, sizeof *roc_sites.rows, roc_row_order);
    roc_sites.loaded = true;
}

// The script's line and column of the site, for the call `call` ("" for
// none): false when the site is not in the script's own lines.
static bool roc_site_place(uintptr_t site, Str call, uint32_t *line, uint32_t *column) {
    if (!roc_sites.loaded || site == 0 || roc_sites.rows_len == 0) return false;
    // The table's address of the site: roc_main moved by as much as it.
    uint64_t addr = (uint64_t)site - (uint64_t)(uintptr_t)roc_main + roc_sites.base;
    size_t lo = 0, hi = roc_sites.rows_len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (roc_sites.rows[mid].addr <= addr) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) return false;
    uint32_t at_line = roc_sites.rows[lo - 1].line;
    if (at_line == 0) return false;
    uint32_t col = 0;
    for (size_t k = 0; k < roc_sites.calls_len && col == 0; k++) {
        const RocCall *c = &roc_sites.calls[k];
        if (c->line == at_line && call.len > 0 && strlen(c->name) == call.len && memcmp(c->name, call.ptr, call.len) == 0) col = c->column;
    }
    if (col == 0 && at_line < roc_sites.first_len) col = roc_sites.first[at_line];
    if (col == 0) col = 1;
    *line = at_line;
    *column = col;
    return true;
}

// The Roc's name of what it was doing when the run stopped: its hosted
// call's (`Shale.read!`), `crash` for a crash, "" for the rest.
static Str roc_doing(void) {
    if (req_kind >= 0 && req_kind < HOSTED_COUNT) return hosted_roc_names[req_kind];
    if (req_kind == REQ_CRASH) return S("crash");
    return S("");
}

static STATUS_MUST_USE Status roc_run_ended(RunCtx *r, Status s) {
    if (s == STOP && r->stopped && roc_started && req_kind != REQ_DONE) {
        uint32_t line = 0, column = 0;
        Str doing = roc_doing();
        if (roc_site_place(req_site, doing, &line, &column)) {
            r->stop.line = line;
            r->stop.column = column;
            // The hosted call it stopped in, as a Shale stop names the
            // function: the driver's own calls are no place of the script's.
            r->stop.function = str_eq(doing, S("crash")) ? S("") : doing;
            r->stop.trail = (Calls){NULL, 0};
            roc_stop_named = true;
        }
    }
    return run_ended(r, s);
}

static uint64_t roc_cpu_ms_used(void) {
    return roc_cpu_ns / 1000000u;
}

// The report: the Roc's script named for a stop put at its line, and the
// Roc's CPU time on the `── used:` line (`, 12 of 5000 Roc CPU ms`), which
// is there too when the Roc used past half of its budget.
static STATUS_MUST_USE Status roc_write_report(Buf *w, Str name, Preludes preludes, bool verbose, Names effects, Names records, const Stop *s, RunUsed u, const Budgets *b, const StepsTop *steps) {
    Str named = name;
    if (roc_stop_named && s != NULL) named = (Str){roc_sites.script, strlen(roc_sites.script)};
    size_t from = w->len;
    TRY(cli_write_report(w, named, preludes, verbose, effects, records, s, u, b, steps));
    Buf said = buf_make(w->arena);
    TRY(buf_lit(&said, ", "));
    TRY(buf_u64(&said, roc_cpu_ms_used()));
    TRY(buf_lit(&said, " of "));
    TRY(buf_u64(&said, roc_cpu_budget_ms));
    TRY(buf_lit(&said, " Roc CPU ms"));
    Str all = buf_view(w);
    Str report = str_sub(all, from, all.len);
    Str mark = S(" output bytes\n");
    for (size_t i = 0; i + mark.len <= report.len; i++) {
        if (!str_eq(str_sub(report, i, i + mark.len), mark)) continue;
        // After `output bytes`, before its newline.
        size_t at_nl = from + i + mark.len - 1;
        Buf tail = buf_make(w->arena);
        TRY(buf_str(&tail, str_sub(all, at_nl, all.len)));
        w->len = at_nl;
        TRY(buf_str(w, buf_view(&said)));
        return buf_str(w, buf_view(&tail));
    }
    if (roc_cpu_ms_used() * 2 <= roc_cpu_budget_ms) return OK;
    TRY(buf_lit(w, "── used: "));
    TRY(buf_str(w, str_sub(buf_view(&said), 2, buf_view(&said).len)));
    return buf_byte(w, '\n');
}

// The stop of the Roc's CPU budget, at `pos` (roc_run_ended moves it to the
// Roc's line when it can).
static STATUS_MUST_USE Status roc_cpu_stop(Run *r, Pos pos) {
    Buf b = buf_make(r->ctx.values);
    Status s = buf_lit(&b, "the Roc's CPU time is past its budget (");
    if (s == OK) s = buf_u64(&b, roc_cpu_budget_ms);
    if (s == OK) s = buf_lit(&b, " ms): raise it with `--budget roc_cpu_ms=N` or the script's `## budget: roc_cpu_ms=N` line");
    Status made = run_fail(&r->ctx, pos, CLASS_BUDGET, s == OK ? buf_view(&b) : S("the Roc's CPU time is past its budget"));
    r->ctx.stop.budget = BUDGET_CPU;
    return made;
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
        roc_stack = stack;
        roc_cpu_start();
        roc_started = true;
    } else {
        if (args.len != 1) return run_failf(r, pos, CLASS_ARGUMENT, "`roc_step` takes the last request's answer", S("`roc_step` takes the last request's answer"));
        if (req_kind < 0 || req_kind >= HOSTED_COUNT) return run_failf(r, pos, CLASS_HOST, "the Roc platform: `roc_step` after the app ended", S("the Roc platform: `roc_step` after the app ended"));
        TRY(hosted_answer_from(r, pos, req_kind, &at(args, 0)));
    }
    // The Roc runs to its next request, timed, under a timer for what is
    // left of its CPU budget.
    uint64_t budget_ns = roc_cpu_budget_ms * 1000000u;
    if (roc_cpu_ns >= budget_ns) return roc_cpu_stop(r, pos);
    uint64_t began = roc_thread_ns();
    roc_timer_armed = 1;
    roc_timer_set(budget_ns - roc_cpu_ns);
    swapcontext(&shale_side, &roc_side);
    roc_timer_armed = 0;
    roc_timer_set(0);
    roc_cpu_ns += roc_thread_ns() - began;
    if (req_kind == REQ_CPU || roc_cpu_ns > budget_ns) return roc_cpu_stop(r, pos);
    if (req_kind == REQ_ARENA) return roc_refused_stop(r, pos);
    return req_value(r, pos, out);
}

static STATUS_MUST_USE Status(host_call)(void *ctx, Run *r, Pos pos, Str name, ConstValues args, Value *out) {
    if (str_eq(name, S("roc_step"))) return roc_step(r, pos, args, out);
    return shale_host_call(ctx, r, pos, name, args, out);
}

// `--budget roc_cpu_ms=N`: the Roc's CPU budget, which the host keeps, not
// Shale's command line. False for any other budget.
static bool roc_budget_taken(const char *spec) {
    const char *name = "roc_cpu_ms=";
    if (strncmp(spec, name, strlen(name)) != 0) return false;
    const char *digits = spec + strlen(name);
    char *end = NULL;
    unsigned long long ms = strtoull(digits, &end, 10);
    if (*digits == 0 || *end != 0 || ms == 0 || ms > 86400000ull) {
        fprintf(stderr, "roc platform: `--budget roc_cpu_ms=%s` is no count of milliseconds from 1 to 86400000\n", digits);
        exit(2);
    }
    roc_cpu_budget_ms = ms;
    return true;
}

// `<program> [--roc-sites FILE] [--budget NAME=N]... [NAME=VALUE]...`:
// `shale -e HOSTED_DRIVER` with the rest of the command line, in the folder
// it runs in. The sites table is the launcher's (roc_run_ended), and the
// `roc_cpu_ms` budget the host's.
int main(int argc, char **argv) {
    char **all = calloc((size_t)argc + 3, sizeof *all);
    if (all == NULL) return 2;
    all[0] = argv[0];
    int n = 1;
    // The `--budget`s come before the script, the values after it.
    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "--roc-sites") == 0 && k + 1 < argc) {
            roc_sites_load(argv[++k]);
        } else if (strcmp(argv[k], "--budget") == 0 && k + 1 < argc) {
            k++;
            if (roc_budget_taken(argv[k])) continue;
            all[n++] = argv[k - 1];
            all[n++] = argv[k];
        }
    }
    all[n++] = "-e";
    all[n++] = (char *)HOSTED_DRIVER;
    for (int k = 1; k < argc; k++) {
        if ((strcmp(argv[k], "--budget") == 0 || strcmp(argv[k], "--roc-sites") == 0) && k + 1 < argc) {
            k++;
            continue;
        }
        all[n++] = argv[k];
    }
    all[n] = NULL;
    return shale_cli_main(n, all);
}
