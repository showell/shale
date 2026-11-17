// The C's half of the comparison with Zig's `run`: runs a program through
// spawn.c and keep.c as `run` would with `timeout_ms` and `tail`, and prints
// the fields of `run`'s answer, then its `out`:
//
//   spawn_probe TIMEOUT_MS TAIL PROGRAM [ARG]...
//
//   ended=exit code=0 ms=12 cpu_ms=1 total_lines=3 matched_lines=3 kept_lines=3
//   <out, as kept>
//
// `ended` is `run`'s word for it (an interrupted run is `interrupted`, where
// Zig's `run` stops the script instead), and `code=-` when it did not exit.
// The environment is PATH alone. c/build.shale runs it beside its own `run`
// of the same program.
#define _GNU_SOURCE
#include "../../../host/keep.c"
#include "../../../host/spawn_argv.c"
#include "../../../host/spawn.c"

static STATUS_MUST_USE Status probe_write(void *ctx, Str bytes) {
    return keep_feed(ctx, bytes);
}

static Str arg_of(char **argv, size_t argc, size_t i) {
    SLICE_TYPE(Args, char *);
    Args args = {argv, argc};
    return (Str){at(args, i), strlen(at(args, i))};
}

int main(int argc, char **argv) {
    if (argc < 4) return 2;
    spawn_quiet_sigpipe();
    Arena a = arena_make((size_t)1 << 30);
    Arena held = arena_make((size_t)1 << 28);
    size_t n = (size_t)argc - 3;
    Str *prog = ARENA_ARRAY(&a, Str, n);
    if (prog == NULL) return 3;
    SLICE_TYPE(Prog, Str);
    Prog p = {prog, n};
    for (size_t i = 0; i < n; i++) at(p, i) = arg_of(argv, (size_t)argc, i + 3);
    const char *path = getenv("PATH");
    Buf env = buf_make(&a);
    if (buf_lit(&env, "PATH=") != OK || buf_str(&env, (Str){path != NULL ? path : "", path != NULL ? strlen(path) : 0}) != OK) return 3;
    Str env_all[1] = {buf_view(&env)};
    SpawnSpec s;
    spawn_spec_default(&s);
    s.argv.ptr = prog;
    s.argv.len = n;
    s.env.ptr = env_all;
    s.env.len = 1;
    s.timeout_ms = (uint64_t)strtoull(arg_of(argv, (size_t)argc, 1).ptr, NULL, 10);
    KeepOptions ko;
    keep_options_default(&ko);
    ko.tail = (size_t)strtoull(arg_of(argv, (size_t)argc, 2).ptr, NULL, 10);
    Keep k;
    if (keep_init(&k, &held, ko) != OK) return 3;
    SpawnSink sink = {&k, probe_write};
    SpawnOutcome o;
    SpawnResult r = spawn_run(&a, &s, sink, &o);
    if (r != SPAWN_OK) {
        printf("spawn=%d\n", (int)r);
        return 1;
    }
    Kept kept;
    if (keep_finish(&k, &kept) != OK) return 3;
    static const char *words_all[4] = {"exit", "timeout", "signal", "interrupted"}; // cstr: literals
    SLICE_TYPE(Words, const char *);
    Words words = {words_all, 4};
    printf("ended=%s code=", at(words, (size_t)o.ended));
    if (o.ended == SPAWN_EXIT) {
        printf("%d", (int)o.code);
    } else {
        printf("-");
    }
    printf(" ms=%llu cpu_ms=%llu total_lines=%llu matched_lines=%llu kept_lines=%llu\n", (unsigned long long)o.ms, (unsigned long long)o.cpu_ms,
           (unsigned long long)kept.total, (unsigned long long)kept.matched, (unsigned long long)kept.kept);
    fwrite(kept.out.ptr, 1, kept.out.len, stdout);
    arena_deinit(&held);
    arena_deinit(&a);
    return 0;
}
