// ran.c: a source parsed, compiled and run as the C `shale` runs it, with no
// preludes, over a fresh folder of its own (c/out/ran-tmp/t.XXXXXX): its
// `root`, the Linux store measured under it, the path hook over the mounts a
// test gives (each `D` in their text the folder), the Linux host's calls and
// `--undo-on-stop`'s putting back. What test_vm.c and
// store/test_builtins.c share.
//
// A test file defines _GNU_SOURCE, includes test.h, then this.

#include <sys/stat.h>

#include "../script/run.h"
#include "../host/store.c"
#include "../host/path_run.c"
#include "../script/regex.c"
#include "../script/builtins.c"
#include "../script/builtins_text.c"
#include "../script/builtins_store.c"
#include "../script/builtins_walk.c"
#include "../script/builtins_change.c"
#include "../script/vm.c"
#include "../host/mounts.c"
#include "../host/paths.c"
#include "../host/undo.c"
#include "../host/spawn.c"
#include "../host/keep.c"
#include "../host/job_record.c"
#include "../host/jobs.c"
#include "../host/host_run.c"
#include "../host/host_jobs.c"
#include "../host/host_json.c"
#include "../host/host_text.c"
#include "../host/zig_wire.c"
#include "../host/host_zig.c"
#include "../host/host_call.c"

// How a run is made: its mounts file's text, `--verbose` (a diff in full)
// and `--undo-on-stop`.
typedef struct {
    const char *mounts; // cstr: a test's literal
    bool verbose;
    bool undo;
} RanOpts;

// One source's run: its arenas, its folder, its parse and code, its store
// and hook, and how it ended.
typedef struct {
    Arena tree;
    Arena values;
    Arena machine;
    Arena output;
    Arena host;
    char dir_bytes[PATH_MAX];
    Str dir;
    Str root;
    LinuxStore linux_store;
    Host lh;
    PathHook hook;
    RunStore store;
    Program prog;
    Code code;
    Run r;
    bool parsed;
    Status ran;
    // What `--undo-on-stop` said, after a stop.
    Str undone;
} Ran;

static Str ran_cs(const char *lit) { // cstr: a test's literal
    return (Str){lit, strlen(lit)};
}

// `lit` with each `D` the run's folder.
static Str ran_d(Ran *x, const char *lit) { // cstr: a test's literal
    Buf b = buf_make(&x->host);
    for (const char *p = lit; *p != 0; p++) {
        Status s = *p == 'D' ? buf_str(&b, x->dir) : buf_byte(&b, *p);
        if (s != OK) abort();
    }
    return buf_view(&b);
}

// `rel` under the folder, as an OS path.
static Str ran_at(Ran *x, const char *rel) { // cstr: a test's literal
    Str out = S("");
    if (!store_join(&x->host, x->dir, ran_cs(rel), &out)) abort();
    return out;
}

// Writes `text` at the folder's `rel`, its folders made.
static void ran_write(Ran *x, const char *rel, Str text) { // cstr: a test's literal
    Str p = ran_at(x, rel);
    Str dir = S("");
    if (store_dirname(p, &dir) && store_os_make_path(&x->host, dir) != STORE_OK) abort();
    if (store_os_write_file(&x->host, p, text, 0644) != STORE_OK) abort();
}

// `inline` here and in ran_file: a test file that writes no file before its
// run, test_vm.c, has no use of either.
static inline void ran_put(Ran *x, const char *rel, const char *text) { // cstr: test literals
    ran_write(x, rel, ran_cs(text));
}

// What the folder's `rel` holds, or false when it cannot be read.
static inline bool ran_file(Ran *x, const char *rel, Str *out) { // cstr: a test's literal
    return store_os_read_file(&x->host, ran_at(x, rel), (size_t)1 << 20, out) == STORE_OK;
}

// A fresh folder with an empty `root`, and the mounts `o` names, loaded as
// the CLI loads them.
static void ran_init(Ran *x, RanOpts o) {
    memset(x, 0, sizeof *x);
    x->host = arena_make((size_t)256 << 20);
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd) == NULL) abort();
    mkdir("c/out", 0777);
    mkdir("c/out/ran-tmp", 0777);
    char tmpl[] = "c/out/ran-tmp/t.XXXXXX";
    if (mkdtemp(tmpl) == NULL) abort();
    int n = snprintf(x->dir_bytes, sizeof x->dir_bytes, "%s/%s", cwd, tmpl);
    if (n <= 0) abort();
    x->dir = (Str){x->dir_bytes, (size_t)n};
    x->root = ran_at(x, "root");
    if (store_os_make_path(&x->host, x->root) != STORE_OK) abort();
    ran_write(x, "mounts", ran_d(x, o.mounts != NULL ? o.mounts : ""));
    MountsLoaded loaded;
    Str why = S("");
    if (mounts_load(&x->host, true, ran_at(x, "mounts"), S("/home/x"), &loaded, &why) != MOUNTS_OK) {
        fprintf(stderr, "mounts: %.*s\n", (int)why.len, why.ptr);
        abort();
    }
    x->linux_store.has_data_base = true;
    x->linux_store.data_base = x->root;
    store_linux(&x->linux_store, &x->store.store);
    paths_make(&x->lh.paths, &x->host, x->root, x->dir, Mounts_items(&loaded.mounts), loaded.from);
    x->lh.paths.undo = o.undo;
    SpawnStrs env = {NULL, 0};
    host_make(&x->lh, env);
    paths_hook(&x->lh.paths, &x->hook);
    x->store.diffs_summary = !o.verbose;
}

// Parses, compiles and runs `src` on the folder `ran_init` made.
static void ran_run(Ran *x, Str src, Budgets b) {
    x->tree = arena_make((size_t)64 << 20);
    x->values = arena_make(b.arena_bytes);
    x->machine = arena_make(b.machine_bytes);
    x->output = arena_make(b.output_bytes + ARENA_BIG);
    Parser p;
    parse_init(&p, &x->tree, src, b.nesting);
    Loader none = {NULL, NULL};
    Preludes no_preludes = {NULL, 0};
    x->parsed = parse_program(&p, S(""), no_preludes, none, &x->prog) == OK;
    if (!x->parsed) return;
    x->parsed = compile_program(&x->tree, &x->prog, BUILTINS_MOST_ARGS, b.depth, &x->code) == OK;
    if (!x->parsed) return;
    Run *r = &x->r;
    r->ctx = run_ctx_make(&x->values, &x->machine, b);
    r->machine = &x->machine;
    r->program = &x->prog;
    r->now = 1700000000;
    r->out = buf_make(&x->output);
    r->out_cap = b.output_bytes;
    r->out.ptr = arena_alloc(&x->output, r->out_cap, 1);
    r->out.cap = r->out_cap;
    if (r->out.ptr == NULL) return;
    r->stack = ValueVec_make(&x->machine);
    r->patterns = regex_cache_make();
    PathRun path = {&r->ctx, x->root, builtin_path_charge, r, &x->hook};
    x->store.path = path;
    x->store.effects = Effects_make(&x->values);
    r->store = &x->store;
    RunHost host = {&x->lh, host_call};
    r->host = host;
    x->ran = run_ended(&r->ctx, vm_main(r, &x->code));
    if (x->ran != OK && x->lh.paths.undo) {
        Buf w = buf_make(&x->host);
        bool all = false;
        if (undo_changes(&x->lh.paths, &w, &all) != OK) abort();
        x->undone = buf_view(&w);
    }
}

static void ran_free(Ran *x) {
    regex_cache_deinit(&x->r.patterns);
    if (store_os_delete_tree(&x->host, x->dir) != STORE_OK) abort();
    arena_deinit(&x->output);
    arena_deinit(&x->machine);
    arena_deinit(&x->values);
    arena_deinit(&x->tree);
    arena_deinit(&x->host);
}

// The report the run prints, as the C `shale` writes it, `verbose` with its
// effects.
static Str ran_report_of(Ran *x, Arena *a, bool verbose) {
    Buf w = buf_make(a);
    Preludes none = {NULL, 0};
    Names effects = {NULL, 0};
    Names records = {x->lh.records.ptr, x->lh.records.len};
    if (verbose && store_effect_lines(&x->store, a, &effects) != OK) return S("(the report ran out of arena)");
    RunUsed u = {x->r.steps, x->r.reads, x->r.read_bytes, x->r.out.len};
    Budgets b = x->r.ctx.budgets;
    if (cli_write_report(&w, S("stdin"), none, verbose, effects, records, x->ran == OK ? NULL : &x->r.ctx.stop, u, &b) != OK) return S("(the report ran out of arena)");
    return buf_view(&w);
}
