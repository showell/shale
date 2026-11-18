// file_system_fixture.c: a fresh root folder under c/out/file-system-tmp, an
// arena, and the file system with no bases, for the file system's tests and
// (through fixture.c) the path hook's. A test file defines _GNU_SOURCE,
// includes test.h, then this.

#include <sys/stat.h>

#include "../../host/path.c"
#include "../../host/file_system.c"

typedef struct {
    Arena values;
    // The file system tests' refusal, outside any run: `values` sets it unless
    // sx_init is given a run's.
    bool refused;
    // The root, absolute.
    char root_bytes[PATH_MAX];
    Str root;
    FileSystem file_system;
} Sx;

// Makes the fixture, its arena setting `*refused` (`&s->refused`, or a run's
// for a fixture inside one).
static void sx_init(Sx *s, bool *refused) {
    memset(s, 0, sizeof *s);
    arena_init(&s->values, (size_t)256 << 20, refused);
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd) == NULL) abort();
    mkdir("c/out", 0777);
    mkdir("c/out/file-system-tmp", 0777);
    char tmpl[] = "c/out/file-system-tmp/t.XXXXXX";
    if (mkdtemp(tmpl) == NULL) abort();
    int n = snprintf(s->root_bytes, sizeof s->root_bytes, "%s/%s", cwd, tmpl);
    if (n <= 0) abort();
    s->root = (Str){s->root_bytes, (size_t)n};
}

static void sx_deinit(Sx *s) {
    bool refused = false;
    Arena a;
    arena_init(&a, (size_t)1 << 20, &refused);
    if (file_system_delete_tree(&a, s->root) != FILE_SYSTEM_OK) abort();
    arena_deinit(&a);
    arena_deinit(&s->values);
}

static Str sx_cs(const char *lit) { // cstr: a test's literal
    return (Str){lit, strlen(lit)};
}

// `rel` under the root, as an OS path.
static Str sx_at(Sx *s, const char *rel) { // cstr: a test's literal
    Str out = S("");
    if (!path_join(&s->values, s->root, sx_cs(rel), &out)) abort();
    return out;
}

static void sx_mkdir(Sx *s, const char *rel) { // cstr: a test's literal
    if (file_system_make_path(sx_at(s, rel)) != FILE_SYSTEM_OK) abort();
}

static void sx_put(Sx *s, const char *rel, const char *text) { // cstr: test literals
    Str p = sx_at(s, rel);
    Str dir = S("");
    if (path_dirname(p, &dir) && file_system_make_path(dir) != FILE_SYSTEM_OK) abort();
    if (file_system_write_file(p, sx_cs(text), 0644) != FILE_SYSTEM_OK) abort();
}

static int sx_chmod(Sx *s, const char *rel, mode_t mode) { // cstr: a test's literal
    FileSystemCPath cp;
    const char *c = NULL;
    if (file_system_cstr(&cp, sx_at(s, rel), &c) != FILE_SYSTEM_OK) abort();
    return chmod(c, mode);
}

// What the root's `rel` holds, or false when it cannot be read.
static bool sx_file(Sx *s, const char *rel, Str *out) { // cstr: a test's literal
    return file_system_read_file(&s->values, sx_at(s, rel), (size_t)1 << 20, out) == FILE_SYSTEM_OK;
}

static bool sx_there(Sx *s, const char *rel) { // cstr: a test's literal
    struct stat st;
    return file_system_stat_exact(sx_at(s, rel), false, &st) == FILE_SYSTEM_OK;
}
