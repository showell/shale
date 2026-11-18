// fixture.c: what the path hook's tests share: store_fixture.c's root and
// store, a run's context with its step charge, the Linux hook over the
// mounts a test parses, and the builtins' path checks in the order the
// builtins make them (`fx_look`, `fx_change`), since the builtins themselves
// need the VM.
//
// A test file defines _GNU_SOURCE, includes test.h, then this.

#include "store_fixture.c"
#include "../../host/path_run.c"
#include "../../host/mounts.c"
#include "../../host/paths.c"
#include "../../host/undo.c"

typedef struct {
    Sx base;
    Arena machine;
    Arena host;
    RunCtx ctx;
    char cwd_bytes[PATH_MAX];
    Str cwd;
    uint64_t steps;
    uint64_t steps_budget;
    Paths paths;
    PathHook hook;
    PathRun run;
} Fx;

static STATUS_MUST_USE Status fx_charge(void *vm, RunCtx *ctx, Pos pos, uint64_t n) {
    Fx *f = vm;
    // A charge past what is left stops one step past the budget, as a run's
    // own charge does.
    if (n > f->steps_budget - f->steps) {
        f->steps = f->steps_budget + 1;
        return run_budget_stop(ctx, pos, BUDGET_STEPS);
    }
    f->steps += n;
    return OK;
}

static Pos fx_pos(void) {
    Pos p = {1, 1};
    return p;
}

// Makes the fixture: a fresh root, no mounts, no undo.
static void fx_init(Fx *f) {
    memset(f, 0, sizeof *f);
    sx_init(&f->base, &f->ctx.refused);
    arena_init(&f->machine, (size_t)16 << 20, &f->ctx.refused);
    arena_init(&f->host, (size_t)256 << 20, &f->ctx.refused);
    f->ctx = run_ctx_make(&f->base.values, &f->machine, budgets_default());
    f->steps_budget = 5000000;
    if (getcwd(f->cwd_bytes, sizeof f->cwd_bytes) == NULL) abort();
    f->cwd = (Str){f->cwd_bytes, strlen(f->cwd_bytes)};
    Mounts none = Mounts_make(&f->host);
    paths_make(&f->paths, &f->host, f->base.root, f->cwd, Mounts_items(&none), S("~/.config/shale/mounts"));
    paths_hook(&f->paths, &f->hook);
    PathRun r = {&f->ctx, f->base.root, fx_charge, f, &f->hook};
    f->run = r;
    if (!paths_secret_dirs_make(&f->paths, &f->run)) abort();
}

static void fx_deinit(Fx *f) {
    sx_deinit(&f->base);
    arena_deinit(&f->machine);
    arena_deinit(&f->host);
}

// `lit` with each `R` the root's path.
static Str fx_r(Fx *f, const char *lit) { // cstr: a test's literal
    Buf b = buf_make(&f->base.values);
    for (const char *p = lit; *p != 0; p++) {
        Status s = *p == 'R' ? buf_str(&b, f->base.root) : buf_byte(&b, *p);
        if (s != OK) abort();
    }
    return buf_view(&b);
}

static void fx_link(Fx *f, const char *target, const char *rel) { // cstr: test literals
    StoreCPath cp;
    const char *c = NULL;
    if (store_cstr(&cp, sx_at(&f->base, rel), &c) != STORE_OK || symlink(target, c) != 0) abort();
}

// The mounts `text` (each `R` the root) as the host's, home `/home/x`.
static void fx_mounts(Fx *f, const char *text) { // cstr: a test's literal
    Mounts ms;
    Str why = S("");
    if (mounts_parse(&f->host, fx_r(f, text), S("/home/x"), &ms, &why) != MOUNTS_OK) {
        fprintf(stderr, "mounts: %.*s\n", (int)why.len, why.ptr);
        abort();
    }
    f->paths.mounts = Mounts_items(&ms);
    if (!paths_secret_dirs_make(&f->paths, &f->run)) abort();
}

// The run made ready for its next call: no stop, the steps kept.
static void fx_again(Fx *f) {
    f->ctx.stopped = false;
    memset(&f->ctx.stop, 0, sizeof f->ctx.stop);
}

// A builtin that only looks, `who(path)`: its path confined with the root
// allowed, then the store's stat, a missing path no stop. `exists`, `stat`,
// `ls` and the reads make these calls in this order.
static STATUS_MUST_USE Status fx_look(Fx *f, const char *who, Str path) { // cstr: a builtin's name
    fx_again(f);
    StorePath p;
    TRY(path_arg(&f->run, fx_pos(), sx_cs(who), path, true, &p));
    bool found = false;
    StoreStat st;
    StoreErr e = f->base.store.stat_or_null(f->base.store.ctx, &f->base.values, p.full, &found, &st);
    if (e != STORE_OK) return path_store_failed(&f->run, fx_pos(), sx_cs(who), p.rel, e);
    return OK;
}

// A change builtin, `who(path, data)`: its path confined (never the root),
// the hook asked, then the store's replace, as a change builtin does.
static STATUS_MUST_USE Status fx_change(Fx *f, const char *who, Str path, Str data) { // cstr: a builtin's name
    fx_again(f);
    StorePath p;
    TRY(path_arg(&f->run, fx_pos(), sx_cs(who), path, false, &p));
    TRY(f->hook.may_change(f->hook.ctx, &f->run, fx_pos(), p.rel, p.full));
    StoreErr e = f->base.store.replace(f->base.store.ctx, &f->base.values, p.full, data);
    if (e != STORE_OK) return path_store_failed(&f->run, fx_pos(), sx_cs(who), p.rel, e);
    return OK;
}

// Whether the run stopped with class `cls` and a message holding `says`;
// prints what it said when not.
static bool fx_stopped(Fx *f, Status s, StopClass cls, Str says) {
    if (s != STOP || !f->ctx.stopped) {
        fprintf(stderr, "  no stop; wanted: %.*s\n", (int)says.len, says.ptr);
        return false;
    }
    Str m = f->ctx.stop.message;
    bool holds = says.len == 0 || mem_find(m.ptr, m.len, says.ptr, says.len) != SIZE_MAX;
    Str cname = stop_class_name(f->ctx.stop.cls);
    if (f->ctx.stop.cls != cls || !holds) {
        fprintf(stderr, "  stopped as %.*s: %.*s\n  wanted: %.*s\n", (int)cname.len, cname.ptr, (int)m.len, m.ptr, (int)says.len, says.ptr);
        return false;
    }
    return true;
}

// Whether the run stopped with class `cls` and exactly the message `says`.
static bool fx_said(Fx *f, Status s, StopClass cls, Str says) {
    if (!fx_stopped(f, s, cls, says)) return false;
    if (!str_eq(f->ctx.stop.message, says)) {
        fprintf(stderr, "  said:   %.*s\n  wanted: %.*s\n", (int)f->ctx.stop.message.len, f->ctx.stop.message.ptr, (int)says.len, says.ptr);
        return false;
    }
    return true;
}
