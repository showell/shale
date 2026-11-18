// ran.c: a source parsed, compiled and run as the C `shale` runs it, with no
// preludes, over a fresh folder of its own (c/out/ran-tmp/t.XXXXXX): its
// `root`, the file system measured under it, the path hook over the mounts a
// test gives (each `D` in their text the folder), the Linux host's calls and
// `--undo-on-stop`'s putting back. What the parts of ran_tests.c share.
//
// ran_tests.c defines _GNU_SOURCE, includes test.h, then this, then its
// parts.

#include <sys/stat.h>

#include "../script/run.h"
#include "../host/path.c"
#include "../host/file_system.c"
#include "../host/path_run.c"
#include "../script/regex.c"
#include "../script/builtins.c"
#include "../script/builtins_text.c"
#include "../script/builtins_files.c"
#include "../script/builtins_walk.c"
#include "../script/builtins_change.c"
#include "../script/vm.c"
#include "../host/mounts.c"
#include "../host/paths.c"
#include "../host/undo.c"
#include "../host/spawn_argv.c"
#include "../host/pgroup.c"
#include "../host/spawn_child.c"
#include "../host/spawn.c"
#include "../host/keep.c"
#include "../host/job_record.c"
#include "../host/jobs.c"
#include "../host/swap_file.c"
#include "../host/host_run.c"
#include "../host/host_jobs.c"
#include "../host/host_procs.c"
#include "../host/host_json.c"
#include "../host/host_text.c"
#include "../host/host_grep_tree.c"
#include "../host/flat_tree.c"
#include "../host/zig_wire.c"
#include "../host/zig_ast.c"
#include "../host/host_zig.c"
#include "../host/host_call.c"

// How a run is made: its mounts file's text, `--verbose` (a diff in full)
// and `--undo-on-stop`.
typedef struct {
    const char *mounts; // cstr: a test's literal
    bool verbose;
    bool undo;
} RanOpts;

// One source's run: its arenas, its folder, its parse and code, its file system
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
    FileSystem file_system;
    Host lh;
    // The run's zig-ast, which ran_free ends.
    HostZigProc zig;
    PathHook hook;
    RunFiles files;
    Program prog;
    Code code;
    Run r;
    bool parsed;
    Status ran;
    // What `--undo-on-stop` said, after a stop.
    Str undone;
    // A run with no host's calls, as metal's: set between ran_init and
    // ran_run.
    bool no_host;
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
    if (!path_join(&x->host, x->dir, ran_cs(rel), &out)) abort();
    return out;
}

// Writes `text` at the folder's `rel`, its folders made.
static void ran_write(Ran *x, const char *rel, Str text) { // cstr: a test's literal
    Str p = ran_at(x, rel);
    Str dir = S("");
    if (path_dirname(p, &dir) && file_system_make_path(dir) != FILE_SYSTEM_OK) abort();
    if (file_system_write_file(p, text, 0644) != FILE_SYSTEM_OK) abort();
}

// Writes `text` at the folder's `rel`.
static void ran_put(Ran *x, const char *rel, const char *text) { // cstr: test literals
    ran_write(x, rel, ran_cs(text));
}

// What the folder's `rel` holds, or false when it cannot be read.
static bool ran_file(Ran *x, const char *rel, Str *out) { // cstr: a test's literal
    return file_system_read_file(&x->host, ran_at(x, rel), (size_t)1 << 20, out) == FILE_SYSTEM_OK;
}

// A fresh folder with an empty `root`, and the mounts `o` names, loaded as
// the CLI loads them.
static void ran_init(Ran *x, RanOpts o) {
    memset(x, 0, sizeof *x);
    // A program that leaves its stdin unread must not end the test.
    spawn_quiet_sigpipe();
    arena_init(&x->host, (size_t)256 << 20, &x->r.ctx.refused);
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
    if (file_system_make_path(x->root) != FILE_SYSTEM_OK) abort();
    ran_write(x, "mounts", ran_d(x, o.mounts != NULL ? o.mounts : ""));
    MountsLoaded loaded;
    Str why = S("");
    if (mounts_load(&x->host, true, ran_at(x, "mounts"), S("/home/x"), &loaded, &why) != MOUNTS_OK) {
        fprintf(stderr, "mounts: %.*s\n", (int)why.len, why.ptr);
        abort();
    }
    x->file_system.has_data_base = true;
    x->file_system.data_base = x->root;
    x->files.file_system = &x->file_system;
    paths_make(&x->lh.paths, &x->host, x->root, x->dir, Mounts_items(&loaded.mounts), loaded.from);
    x->lh.paths.undo = o.undo;
    SpawnStrs env = {NULL, 0};
    host_zig_init(&x->zig);
    host_make(&x->lh, env, &x->zig);
    paths_hook(&x->lh.paths, &x->hook);
    x->files.diffs_summary = !o.verbose;
}

// Parses, compiles and runs `src` on the folder `ran_init` made.
static void ran_run(Ran *x, Str src, Budgets b) {
    // Every arena sets the run's refusal, which run_ctx_make below clears.
    bool *refused = &x->r.ctx.refused;
    arena_init(&x->tree, (size_t)64 << 20, refused);
    arena_init(&x->values, b.arena_bytes, refused);
    arena_init(&x->machine, b.machine_bytes, refused);
    arena_init(&x->output, b.output_bytes + ARENA_BIG, refused);
    Parser p;
    parse_init(&p, &x->tree, src, b.nesting);
    ParseLoader none = {NULL, NULL};
    Preludes no_preludes = {NULL, 0};
    x->parsed = parse_program(&p, S(""), no_preludes, none, &x->prog) == OK;
    if (!x->parsed) return;
    x->parsed = compile_program(&x->tree, &x->prog, BUILTINS_ARGS_MAX, b.depth, &x->code) == OK;
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
    regex_cache_init(&r->patterns, &r->ctx.refused);
    PathRun path = path_run_make(&r->ctx, x->root, builtin_path_charge, r, &x->hook);
    x->files.path = path;
    if (!paths_run_folders_make(&x->lh.paths, &x->files.path)) abort();
    x->files.effects = Effects_make(&x->values);
    r->files = &x->files;
    RunHost host = {&x->lh, host_call};
    RunHost metal = {NULL, NULL};
    r->host = x->no_host ? metal : host;
    x->ran = run_ended(&r->ctx, vm_main(r, &x->code));
    if (x->ran != OK && x->lh.paths.undo) {
        Buf w = buf_make(&x->host);
        bool all = false;
        if (undo_changes(&x->lh.paths, &w, &all) != OK) abort();
        x->undone = buf_view(&w);
    }
}

// Prints each path left under `dir` to stderr, so a folder that would not
// delete says what kept it.
static void ran_say_left(Arena *a, Str dir) {
    FileSystemCPath cp;
    const char *c = NULL;
    if (file_system_cstr(&cp, dir, &c) != FILE_SYSTEM_OK) return;
    DIR *d = opendir(c);
    if (d == NULL) return;
    struct dirent *ent = readdir(d);
    while (ent != NULL) {
        Str n = {ent->d_name, strlen(ent->d_name)};
        Str child = S("");
        if (!str_eq(n, S(".")) && !str_eq(n, S("..")) && path_join(a, dir, n, &child)) {
            fprintf(stderr, "  left: %.*s\n", (int)child.len, child.ptr);
            if (ent->d_type == DT_DIR) ran_say_left(a, child);
        }
        ent = readdir(d);
    }
    closedir(d);
}

// How long ran_free waits for a job's reaper to end.
enum { RAN_REAPER_WAIT_MS = 10000, RAN_REAPER_TICK_MS = 10 };

// Waits for the reaper of each job the run started to end. A reaper writes
// its job's end into the record beside the log after the job exits, so a
// folder deleted while one runs gets that record back and will not delete. A
// record that cannot be read, or a reaper still there after
// RAN_REAPER_WAIT_MS (a job the test left running), ends the test, said on
// stderr.
static void ran_wait_reapers(Ran *x) {
    for (size_t i = 0; i < x->lh.started.len; i++) {
        const HostStarted *j = &at(x->lh.started, i);
        char *p = arena_cstr(&x->host, j->record_os);
        Bytes text = {NULL, 0};
        JobRecord rec;
        if (p == NULL || !jobs_read_small(&x->host, p, &text) || !job_record_parse(text, &rec)) {
            fprintf(stderr, "ran_free: the record of `%.*s` at %.*s cannot be read, so its reaper cannot be waited for\n", (int)j->argv.len, j->argv.ptr, (int)j->record_os.len, j->record_os.ptr);
            abort();
        }
        uint64_t waited = 0;
        while (jobs_reaper_alive(&rec)) {
            if (waited >= RAN_REAPER_WAIT_MS) {
                fprintf(stderr, "ran_free: the reaper of `%.*s` (pid %d) still runs after %d ms: the test left its job running\n", (int)j->argv.len, j->argv.ptr, (int)rec.reaper, RAN_REAPER_WAIT_MS);
                abort();
            }
            jobs_sleep_ms(RAN_REAPER_TICK_MS);
            waited += RAN_REAPER_TICK_MS;
        }
    }
}

static void ran_free(Ran *x) {
    // A Zig call's zig-ast ends with the run, as the CLI ends it.
    host_zig_stop(&x->zig);
    regex_cache_deinit(&x->r.patterns);
    ran_wait_reapers(x);
    FileSystemErr del = file_system_delete_tree(&x->host, x->dir);
    if (del != FILE_SYSTEM_OK) {
        Str why = file_system_err_name(del);
        fprintf(stderr, "ran_free: %.*s could not be deleted: %.*s\n", (int)x->dir.len, x->dir.ptr, (int)why.len, why.ptr);
        ran_say_left(&x->host, x->dir);
        abort();
    }
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
    if (verbose && builtin_effect_lines(&x->files, a, &effects) != OK) return S("(the report ran out of arena)");
    RunUsed u = {x->r.steps, x->r.reads, x->r.read_bytes, x->r.out.len};
    Budgets b = x->r.ctx.budgets;
    StepsTop steps;
    vm_steps_top(&x->r, &x->code, &steps);
    if (cli_write_report(&w, S("stdin"), none, verbose, effects, records, x->ran == OK ? NULL : &x->r.ctx.stop, u, &b, &steps) != OK) return S("(the report ran out of arena)");
    return buf_view(&w);
}

// A source run on a fresh root with no mounts.
static void ran_source(Ran *x, Str src, Budgets b) {
    RanOpts none = {NULL, false, false};
    ran_init(x, none);
    ran_run(x, src, b);
}

// The CLI's budgets but for the output, 64 KiB as `shale` unset has it.
static Budgets ran_budgets(void) {
    Budgets b = budgets_default();
    b.output_bytes = 64 * 1024;
    return b;
}

// The report a run's stop prints, as the C `shale` writes it.
static Str ran_report(Ran *x, Arena *a) {
    return ran_report_of(x, a, false);
}
