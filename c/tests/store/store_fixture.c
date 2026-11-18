// store_fixture.c: a fresh root folder under c/out/store-tmp, an arena, and
// the Linux store, for the store's tests and (through fixture.c) the path
// hook's. A test file defines _GNU_SOURCE, includes test.h, then this.

#include <sys/stat.h>

#include "../../host/store.c"

typedef struct {
    Arena values;
    // The root, absolute.
    char root_bytes[PATH_MAX];
    Str root;
    LinuxStore linux_store;
    Store store;
} Sx;

static void sx_init(Sx *s) {
    memset(s, 0, sizeof *s);
    arena_init(&s->values, (size_t)256 << 20);
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd) == NULL) abort();
    mkdir("c/out", 0777);
    mkdir("c/out/store-tmp", 0777);
    char tmpl[] = "c/out/store-tmp/t.XXXXXX";
    if (mkdtemp(tmpl) == NULL) abort();
    int n = snprintf(s->root_bytes, sizeof s->root_bytes, "%s/%s", cwd, tmpl);
    if (n <= 0) abort();
    s->root = (Str){s->root_bytes, (size_t)n};
    store_linux(&s->linux_store, &s->store);
}

static void sx_deinit(Sx *s) {
    Arena a;
    arena_init(&a, (size_t)1 << 20);
    if (store_os_delete_tree(&a, s->root) != STORE_OK) abort();
    arena_deinit(&a);
    arena_deinit(&s->values);
}

static Str sx_cs(const char *lit) { // cstr: a test's literal
    return (Str){lit, strlen(lit)};
}

// `rel` under the root, as an OS path.
static Str sx_at(Sx *s, const char *rel) { // cstr: a test's literal
    Str out = S("");
    if (!store_join(&s->values, s->root, sx_cs(rel), &out)) abort();
    return out;
}

static void sx_mkdir(Sx *s, const char *rel) { // cstr: a test's literal
    if (store_os_make_path(sx_at(s, rel)) != STORE_OK) abort();
}

static void sx_put(Sx *s, const char *rel, const char *text) { // cstr: test literals
    Str p = sx_at(s, rel);
    Str dir = S("");
    if (store_dirname(p, &dir) && store_os_make_path(dir) != STORE_OK) abort();
    if (store_os_write_file(p, sx_cs(text), 0644) != STORE_OK) abort();
}

static int sx_chmod(Sx *s, const char *rel, mode_t mode) { // cstr: a test's literal
    StoreCPath cp;
    const char *c = NULL;
    if (store_cstr(&cp, sx_at(s, rel), &c) != STORE_OK) abort();
    return chmod(c, mode);
}

// What the root's `rel` holds, or false when it cannot be read.
static bool sx_file(Sx *s, const char *rel, Str *out) { // cstr: a test's literal
    return store_os_read_file(&s->values, sx_at(s, rel), (size_t)1 << 20, out) == STORE_OK;
}

static bool sx_there(Sx *s, const char *rel) { // cstr: a test's literal
    struct stat st;
    return store_os_stat(sx_at(s, rel), false, &st) == STORE_OK;
}
