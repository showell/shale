// store.c: the store, every file a script reads or changes, on Linux,
// held to the bare-metal store's rules. FAT's rules hold on every host: a
// name FAT holds (`store_fat_name`), case that does not tell two names apart
// (a missing name is looked for in another case, and a new one keeps the
// case it was given), and, under the host's two bases, the bare-metal host's
// limits on a path's length and depth.
//
// Its calls are the ones Shale's builtins make: stat, stat-or-null, has,
// list, read-at, replace (never torn), append, make-dir, remove and
// remove-tree, each given as a `Store` vtable (`store_linux`). An error is a
// `StoreErr`, whose name is Zig's (`store_err_name`), since a stop prints
// it. The store refuses no path for where it is: the root's and the mounts'
// confinement is the caller's (path_run.c, paths.c), checked before any call
// here.
//
// The including file defines _GNU_SOURCE before its first include, for
// `openat`'s and `realpath`'s declarations.

#ifndef _GNU_SOURCE
#error "store.c needs _GNU_SOURCE defined before the first include: realpath, openat, fdopendir"
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include "buf.h"

// A store call's failure, by the name Zig gives it: STORE_OK is none.
typedef enum {
    STORE_OK,
    STORE_FILE_NOT_FOUND,
    STORE_NOT_DIR,
    STORE_IS_DIR,
    STORE_NOT_FILE,
    STORE_PATH_ALREADY_EXISTS,
    STORE_DIR_NOT_EMPTY,
    STORE_ACCESS_DENIED,
    STORE_PERMISSION_DENIED,
    STORE_NO_SPACE_LEFT,
    STORE_SYM_LINK_LOOP,
    STORE_NAME_TOO_LONG,
    STORE_BAD_NAME,
    STORE_BAD_PATH_NAME,
    STORE_PATH_TOO_LONG,
    STORE_PATH_TOO_DEEP,
    STORE_READ_ONLY_FILE_SYSTEM,
    STORE_FILE_BUSY,
    STORE_INPUT_OUTPUT,
    STORE_SYSTEM_RESOURCES,
    STORE_PROCESS_FD_QUOTA_EXCEEDED,
    STORE_SYSTEM_FD_QUOTA_EXCEEDED,
    STORE_DISK_QUOTA,
    STORE_FILE_TOO_BIG,
    STORE_CROSS_DEVICE,
    STORE_STREAM_TOO_LONG,
    STORE_OUT_OF_MEMORY,
    STORE_UNEXPECTED,
    STORE_ERR_COUNT,
} StoreErr;

// The error's name as Zig's `@errorName` gives it. The names follow the
// enum's order.
static Str store_err_name(StoreErr e) {
    static const Str names[STORE_ERR_COUNT] = {
        S(""), S("FileNotFound"), S("NotDir"), S("IsDir"), S("NotFile"), S("PathAlreadyExists"),
        S("DirNotEmpty"), S("AccessDenied"), S("PermissionDenied"), S("NoSpaceLeft"), S("SymLinkLoop"),
        S("NameTooLong"), S("BadName"), S("BadPathName"), S("PathTooLong"), S("PathTooDeep"),
        S("ReadOnlyFileSystem"), S("FileBusy"), S("InputOutput"), S("SystemResources"),
        S("ProcessFdQuotaExceeded"), S("SystemFdQuotaExceeded"), S("DiskQuota"), S("FileTooBig"),
        S("CrossDevice"), S("StreamTooLong"), S("OutOfMemory"), S("Unexpected"),
    };
    SLICE_TYPE(ErrNames, const Str);
    ErrNames n = {names, STORE_ERR_COUNT};
    return at(n, (size_t)e);
}

// An errno as the error std.Io gives for it. Zig maps a few per call (an
// EINVAL), and those reach no message Shale prints, so one table serves.
static StoreErr store_err_of(int e) {
    switch (e) {
    case ENOENT: return STORE_FILE_NOT_FOUND;
    case ENOTDIR: return STORE_NOT_DIR;
    case EISDIR: return STORE_IS_DIR;
    case EEXIST: return STORE_PATH_ALREADY_EXISTS;
    case ENOTEMPTY: return STORE_DIR_NOT_EMPTY;
    case EACCES: return STORE_ACCESS_DENIED;
    case EPERM: return STORE_PERMISSION_DENIED;
    case ENOSPC: return STORE_NO_SPACE_LEFT;
    case ELOOP: return STORE_SYM_LINK_LOOP;
    case ENAMETOOLONG: return STORE_NAME_TOO_LONG;
    case EROFS: return STORE_READ_ONLY_FILE_SYSTEM;
    case EBUSY: return STORE_FILE_BUSY;
    case ETXTBSY: return STORE_FILE_BUSY;
    case EIO: return STORE_INPUT_OUTPUT;
    case ENOMEM: return STORE_SYSTEM_RESOURCES;
    case EMFILE: return STORE_PROCESS_FD_QUOTA_EXCEEDED;
    case ENFILE: return STORE_SYSTEM_FD_QUOTA_EXCEEDED;
    case EDQUOT: return STORE_DISK_QUOTA;
    case EFBIG: return STORE_FILE_TOO_BIG;
    case EXDEV: return STORE_CROSS_DEVICE;
    default: return STORE_UNEXPECTED;
    }
}

// The longest name the bare-metal host reads and writes, its longest path,
// and the most parts a path has from the volume's root.
enum {
    STORE_MAX_NAME = 96,
    STORE_MAX_PATH = 256,
    STORE_MAX_DEPTH = 16,
};

// Whether FAT, as the bare-metal host holds it, can store `name`: 1 to 96
// printable ASCII bytes, none of `" * / : < > ? \ |`, not `.` or `..`, not
// ending in a dot or a space.
static bool store_fat_name(Str name) {
    if (name.len == 0 || name.len > STORE_MAX_NAME) return false;
    if (str_eq(name, S(".")) || str_eq(name, S(".."))) return false;
    for (size_t i = 0; i < name.len; i++) {
        uint8_t c = str_byte(name, i);
        if (c < 0x20 || c > 0x7E) return false;
        if (mem_find_byte("\"*/:<>?\\|", c, 9) != NULL) return false;
    }
    uint8_t last = str_byte(name, name.len - 1);
    return last != '.' && last != ' ';
}

// ── paths, as Zig's std.fs.path spells them ─────────────────────────────

// The last part's start and end in `p`, trailing slashes passed by: Zig's
// component iterator's `last`. False when `p` has no part.
static bool store_last_part(Str p, size_t *from, size_t *to) {
    size_t end = p.len;
    while (end > 0 && str_byte(p, end - 1) == '/') end--;
    if (end == 0) return false;
    size_t start = end;
    while (start > 0 && str_byte(p, start - 1) != '/') start--;
    *from = start;
    *to = end;
    return true;
}

// `p`'s last part, "" when there is none.
static Str store_basename(Str p) {
    size_t from = 0;
    size_t to = 0;
    if (!store_last_part(p, &from, &to)) return S("");
    return str_sub(p, from, to);
}

// Zig's `dirnamePosix`: `p` up to the end of the part before the last; the
// root `/` for an absolute path with one part; false for none.
static bool store_dirname(Str p, Str *out) {
    size_t from = 0;
    size_t to = 0;
    if (!store_last_part(p, &from, &to)) return false;
    size_t end = from;
    while (end > 0 && str_byte(p, end - 1) == '/') end--;
    if (end == 0) {
        if (from > 0) {
            *out = str_sub(p, 0, 1);
            return true;
        }
        return false;
    }
    *out = str_sub(p, 0, end);
    return true;
}

// `dir` and `name` joined by one `/`, as Zig's `path.join` of two parts
// does when `name` has no leading slash.
static bool store_join(Arena *a, Str dir, Str name, Str *out) {
    Buf b = buf_make(a);
    bool slash = dir.len > 0 && str_byte(dir, dir.len - 1) != '/';
    if (buf_str(&b, dir) != OK) return false;
    if (slash && buf_byte(&b, '/') != OK) return false;
    if (buf_str(&b, name) != OK) return false;
    buf_fit(&b);
    *out = buf_view(&b);
    return true;
}

// A path ended by a NUL, for the kernel, on the caller's stack: Zig's
// posix layer copies a path into a PATH_MAX buffer on its stack for each
// call. A copy in the arena outlived the call, so a run of many changes,
// each a score of calls into the kernel, held about twice the arena Zig
// did.
typedef struct {
    char bytes[PATH_MAX];
} StoreCPath;

// `s` ended by a NUL in `buf`, into `*out`: NameTooLong past PATH_MAX, as
// Zig's posix layer answers. `*out` lives as long as `buf`.
// DRAGON: the store's boundaries. A NUL inside `s` would end the kernel's
// copy early, so the call would act on a shorter path than the one every
// check judged (`a\0/../x` checked as one, opened as `a`).
// REMEDY: refused here as Zig's posix layer refuses it, BadPathName, before
// any call; test_store plants it.
static StoreErr store_cstr(StoreCPath *buf, Str s, const char **out) {
    // A path that does not fit with its NUL is NameTooLong.
    if (s.len >= PATH_MAX) return STORE_NAME_TOO_LONG;
    if (str_find_byte(s, 0) != NULL) return STORE_BAD_PATH_NAME;
    Bytes room = {buf->bytes, sizeof buf->bytes};
    if (s.len > 0) mem_copy(room.ptr, s.ptr, s.len);
    at(room, s.len) = 0;
    *out = buf->bytes;
    return STORE_OK;
}

// The errno the kernel's call would have failed with on a path store_cstr
// refused, so its caller's own error path says it: ENAMETOOLONG past
// PATH_MAX, EINVAL for a NUL inside. Neither is an arena's refusal.
static inline int store_cstr_errno(StoreErr e) {
    return e == STORE_NAME_TOO_LONG ? ENAMETOOLONG : EINVAL;
}

// ── the bare-metal host's limits ────────────────────────────────────────

// The two roots as the bare-metal host spells them, `data` and `auth`: a
// path under one is measured as that host would hold it. Neither is set
// under `shale` (only the site's runner sets them), so a CLI run measures
// nothing. And the stat a store call answers.
typedef struct {
    bool has_data_base;
    Str data_base;
    bool has_auth_base;
    Str auth_base;
} LinuxStore;

// Refuses a path the bare-metal host could not hold, measured as it would
// spell it: an empty part (`//` or a trailing `/`) is BadName, past
// STORE_MAX_PATH bytes PathTooLong, past STORE_MAX_DEPTH parts PathTooDeep.
// A path under neither base is not measured.
static StoreErr store_within_limits(const LinuxStore *s, Str path) {
    for (int k = 0; k < 2; k++) {
        bool has = k == 0 ? s->has_data_base : s->has_auth_base;
        if (!has) continue;
        Str base = k == 0 ? s->data_base : s->auth_base;
        while (base.len > 0 && str_byte(base, base.len - 1) == '/') base.len--;
        if (!str_starts_with(path, base)) continue;
        Str rest = str_sub(path, base.len, path.len);
        if (rest.len != 0 && str_byte(rest, 0) != '/') continue;
        size_t depth = 1;
        bool empty_part = rest.len > 0 && str_byte(rest, rest.len - 1) == '/';
        for (size_t i = 0; i < rest.len; i++) {
            bool slash = str_byte(rest, i) == '/';
            if (!slash && (i == 0 || str_byte(rest, i - 1) == '/')) depth++;
            if (slash && i > 0 && str_byte(rest, i - 1) == '/') empty_part = true;
        }
        if (empty_part) return STORE_BAD_NAME;
        if (4 + rest.len > STORE_MAX_PATH) return STORE_PATH_TOO_LONG;
        if (depth > STORE_MAX_DEPTH) return STORE_PATH_TOO_DEEP;
        return STORE_OK;
    }
    return STORE_OK;
}

// ── finding a name in another case ──────────────────────────────────────

static bool store_exists(Str path) {
    StoreCPath cp;
    const char *c = NULL;
    if (store_cstr(&cp, path, &c) != STORE_OK) return false;
    return access(c, F_OK) == 0;
}

static bool store_eq_nocase(Str x, Str y) {
    if (x.len != y.len) return false;
    for (size_t i = 0; i < x.len; i++) {
        uint8_t p = str_byte(x, i);
        uint8_t q = str_byte(y, i);
        if (p >= 'A' && p <= 'Z') p = (uint8_t)(p + 32);
        if (q >= 'A' && q <= 'Z') q = (uint8_t)(q + 32);
        if (p != q) return false;
    }
    return true;
}

// The name in `dir` that is `name` apart from case, in the order the disk
// gives them, into `*found`; `*has` false when there is none or `dir` is
// not a folder.
static StoreErr store_sibling(Arena *a, Str dir, Str name, bool *has, Str *found) {
    *has = false;
    StoreCPath cp;
    const char *c = NULL;
    StoreErr e = store_cstr(&cp, dir.len == 0 ? S(".") : dir, &c);
    if (e != STORE_OK) return e;
    DIR *d = opendir(c);
    if (d == NULL) {
        if (errno == ENOENT || errno == ENOTDIR) return STORE_OK;
        return store_err_of(errno);
    }
    StoreErr got = STORE_OK;
    for (;;) {
        errno = 0;
        struct dirent *ent = readdir(d);
        if (ent == NULL) {
            if (errno != 0) got = store_err_of(errno);
            break;
        }
        Str n = {ent->d_name, strlen(ent->d_name)};
        if (str_eq(n, S(".")) || str_eq(n, S(".."))) continue;
        if (store_eq_nocase(n, name)) {
            if (!arena_dup(a, n, found)) {
                got = STORE_OUT_OF_MEMORY;
            } else {
                *has = true;
            }
            break;
        }
    }
    closedir(d);
    return got;
}

// `path` as it is on disk: the first part not found, and every one after
// it, is looked for in another case; a part found in no case is kept as
// given.
static StoreErr store_resolve(Arena *a, Str path, Str *out) {
    if (store_exists(path)) {
        *out = path;
        return STORE_OK;
    }
    Str parent = S("");
    if (!store_dirname(path, &parent)) parent = S("");
    Str name = store_basename(path);
    Str dir = parent;
    if (parent.len > 0) {
        StoreErr e = store_resolve(a, parent, &dir);
        if (e != STORE_OK) return e;
    }
    bool has = false;
    Str found = name;
    StoreErr e = store_sibling(a, dir, name, &has, &found);
    if (e != STORE_OK) return e;
    if (!has) found = name;
    if (dir.len == 0) {
        *out = found;
        return STORE_OK;
    }
    return store_join(a, dir, found, out) ? STORE_OK : STORE_OUT_OF_MEMORY;
}

// A lookup that missed: the same path in another case, into `*out` with
// `*has` true, or `*has` false when there is no such thing in any case.
// Zig's `retry`, which stats with `statFile` (a path through a file in
// another case is NotDir here).
static StoreErr store_retry(Arena *a, Str path, bool *has, Str *out) {
    *has = false;
    Str p = path;
    StoreErr e = store_resolve(a, path, &p);
    if (e != STORE_OK) return e;
    if (str_eq(p, path)) return STORE_OK;
    StoreCPath cp;
    const char *c = NULL;
    e = store_cstr(&cp, p, &c);
    if (e != STORE_OK) return e;
    struct stat st;
    if (stat(c, &st) != 0) {
        if (errno == ENOENT) return STORE_OK;
        return store_err_of(errno);
    }
    *has = true;
    *out = p;
    return STORE_OK;
}

// `path` resolved for a call that may create its last part, which must be
// a name FAT holds.
static StoreErr store_for_write(const LinuxStore *s, Arena *a, Str path, Str *out) {
    if (!store_fat_name(store_basename(path))) return STORE_BAD_NAME;
    StoreErr e = store_within_limits(s, path);
    if (e != STORE_OK) return e;
    return store_resolve(a, path, out);
}

// `path` resolved for a call that makes folders: every part past what is
// there must be a name FAT holds.
static StoreErr store_for_make_dir(const LinuxStore *s, Arena *a, Str path, Str *out) {
    StoreErr e = store_within_limits(s, path);
    if (e != STORE_OK) return e;
    Str p = path;
    e = store_resolve(a, path, &p);
    if (e != STORE_OK) return e;
    Str rest = p;
    while (!store_exists(rest)) {
        if (!store_fat_name(store_basename(rest))) return STORE_BAD_NAME;
        if (!store_dirname(rest, &rest)) break;
    }
    *out = p;
    return STORE_OK;
}

// ── the operating system's calls, as std.Io makes them ──────────────────

typedef enum {
    STORE_KIND_FILE,
    STORE_KIND_DIRECTORY,
    STORE_KIND_OTHER,
} StoreKind;

// What `stat` answers, as Zig's `Stat`: the size, the kind, and the
// modification time in whole seconds and their nanoseconds.
typedef struct {
    uint64_t size;
    StoreKind kind;
    int64_t mtime_s;
    int64_t mtime_ns;
} StoreStat;

static StoreKind store_kind_of(mode_t m) {
    if (S_ISREG(m)) return STORE_KIND_FILE;
    if (S_ISDIR(m)) return STORE_KIND_DIRECTORY;
    return STORE_KIND_OTHER;
}

static StoreErr store_os_stat(Str path, bool follow, struct stat *st) {
    StoreCPath cp;
    const char *c = NULL;
    StoreErr e = store_cstr(&cp, path, &c);
    if (e != STORE_OK) return e;
    int r = follow ? stat(c, st) : lstat(c, st);
    return r == 0 ? STORE_OK : store_err_of(errno);
}

// Makes `path` hold `data`, created with `mode` if it is not there (the
// umask applies, as Zig's `writeFile`); a file there keeps its own mode.
static StoreErr store_os_write_file(Str path, Str data, mode_t mode) {
    StoreCPath cp;
    const char *c = NULL;
    StoreErr e = store_cstr(&cp, path, &c);
    if (e != STORE_OK) return e;
    int fd = open(c, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) return store_err_of(errno);
    size_t done = 0;
    StoreErr got = STORE_OK;
    while (done < data.len) {
        Str part = str_sub(data, done, data.len);
        ssize_t n = write(fd, part.ptr, part.len);
        if (n < 0) {
            if (errno == EINTR) continue;
            got = store_err_of(errno);
            break;
        }
        done += (size_t)n;
    }
    close(fd);
    return got;
}

// The whole file at `path` in `a`, at most `limit` bytes: one more is
// StreamTooLong, and a refusal of the arena OutOfMemory, as Zig's
// `readFileAlloc` with `.limited(limit)` answers.
static StoreErr store_os_read_file(Arena *a, Str path, size_t limit, Str *out) {
    StoreCPath cp;
    const char *c = NULL;
    StoreErr e = store_cstr(&cp, path, &c);
    if (e != STORE_OK) return e;
    int fd = open(c, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return store_err_of(errno);
    Buf b = buf_make(a);
    StoreErr got = STORE_OK;
    char chunk[16384];
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n < 0) {
            if (errno == EINTR) continue;
            got = store_err_of(errno);
            break;
        }
        if (n == 0) break;
        if ((size_t)n > limit - b.len) {
            got = STORE_STREAM_TOO_LONG;
            break;
        }
        if (buf_str(&b, (Str){chunk, (size_t)n}) != OK) {
            got = STORE_OUT_OF_MEMORY;
            break;
        }
    }
    close(fd);
    if (got == STORE_OK) *out = buf_view(&b);
    return got;
}

// Removes the file at `path`: IsDir for a folder, as Linux's `unlink`
// answers it.
static StoreErr store_os_delete_file(Str path) {
    StoreCPath cp;
    const char *c = NULL;
    StoreErr e = store_cstr(&cp, path, &c);
    if (e != STORE_OK) return e;
    return unlink(c) == 0 ? STORE_OK : store_err_of(errno);
}

// The folder `path` opened to be listed, following no link, into `*fd`.
// Its own function, so the recursion of store_os_delete_tree holds no path
// buffer of its own a level.
static StoreErr store_os_open_dir(Str path, int *fd) {
    StoreCPath cp;
    const char *c = NULL;
    StoreErr e = store_cstr(&cp, path, &c);
    if (e != STORE_OK) return e;
    // DRAGON: the store's boundaries. A folder swapped for a link to another
    // tree between store_os_delete_tree's unlink and this open would be
    // entered and emptied.
    // REMEDY: O_NOFOLLOW on the folder; a link met inside is unlinked as a
    // file by the recursion's first step, never followed.
    *fd = open(c, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    return *fd < 0 ? store_err_of(errno) : STORE_OK;
}

// Removes the empty folder `path`.
static StoreErr store_os_remove_dir(Str path) {
    StoreCPath cp;
    const char *c = NULL;
    StoreErr e = store_cstr(&cp, path, &c);
    if (e != STORE_OK) return e;
    return rmdir(c) == 0 ? STORE_OK : store_err_of(errno);
}

// Removes `path` and everything under it, following no link (a link is
// removed, never entered); nothing there is done.
static StoreErr store_os_delete_tree(Arena *a, Str path) {
    StoreErr e = store_os_delete_file(path);
    if (e == STORE_OK || e == STORE_FILE_NOT_FOUND) return STORE_OK;
    if (e != STORE_IS_DIR) return e;
    int fd = -1;
    e = store_os_open_dir(path, &fd);
    if (e != STORE_OK) return e;
    DIR *d = fdopendir(fd);
    if (d == NULL) {
        close(fd);
        return store_err_of(errno);
    }
    StoreErr got = STORE_OK;
    for (;;) {
        errno = 0;
        struct dirent *ent = readdir(d);
        if (ent == NULL) {
            if (errno != 0) got = store_err_of(errno);
            break;
        }
        Str n = {ent->d_name, strlen(ent->d_name)};
        if (str_eq(n, S(".")) || str_eq(n, S(".."))) continue;
        Str child = S("");
        if (!store_join(a, path, n, &child)) {
            got = STORE_OUT_OF_MEMORY;
            break;
        }
        got = store_os_delete_tree(a, child);
        if (got != STORE_OK) break;
    }
    closedir(d);
    if (got != STORE_OK) return got;
    return store_os_remove_dir(path);
}

// Makes the one folder `path`: done when a folder is there already, NotDir
// for a file in the way, and `*missing` true when its parent is not there.
// Its own function, so the recursion of store_os_make_path holds no path
// buffer of its own a level.
static StoreErr store_os_make_dir_one(Str path, bool *missing) {
    *missing = false;
    StoreCPath cp;
    const char *c = NULL;
    StoreErr e = store_cstr(&cp, path, &c);
    if (e != STORE_OK) return e;
    if (mkdir(c, 0777) == 0) return STORE_OK;
    int err = errno;
    if (err == EEXIST) {
        struct stat st;
        if (stat(c, &st) != 0) return store_err_of(errno);
        return S_ISDIR(st.st_mode) ? STORE_OK : STORE_NOT_DIR;
    }
    *missing = err == ENOENT;
    return store_err_of(err);
}

// Makes the folder `path` and every folder above it that is missing; a
// file in the way is NotDir.
static StoreErr store_os_make_path(Str path) {
    bool missing = false;
    StoreErr e = store_os_make_dir_one(path, &missing);
    if (!missing) return e;
    Str parent = S("");
    if (!store_dirname(path, &parent)) return STORE_FILE_NOT_FOUND;
    e = store_os_make_path(parent);
    if (e != STORE_OK) return e;
    return store_os_make_dir_one(path, &missing);
}

// ── Wyhash, for a replace's sibling name ────────────────────────────────

// The 128-bit product of `*a` and `*b`, its low half to `*a` and its high
// to `*b`, in 64-bit halves: TCC has no 128-bit integer.
static void store_wy_mum(uint64_t *a, uint64_t *b) {
    uint64_t x = *a;
    uint64_t y = *b;
    uint64_t xl = x & 0xffffffffu;
    uint64_t xh = x >> 32;
    uint64_t yl = y & 0xffffffffu;
    uint64_t yh = y >> 32;
    uint64_t ll = xl * yl;
    uint64_t lh = xl * yh;
    uint64_t hl = xh * yl;
    uint64_t hh = xh * yh;
    uint64_t mid = (ll >> 32) + (lh & 0xffffffffu) + (hl & 0xffffffffu);
    *a = (ll & 0xffffffffu) | (mid << 32);
    *b = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}

static uint64_t store_wy_mix(uint64_t a, uint64_t b) {
    store_wy_mum(&a, &b);
    return a ^ b;
}

// `bytes` (4 or 8) little-endian bytes of `s` from `from`.
static uint64_t store_wy_read(Str s, size_t from, size_t bytes) {
    uint64_t v = 0;
    for (size_t k = bytes; k > 0; k--) v = (v << 8) | str_byte(s, from + k - 1);
    return v;
}

// Zig's `std.hash.Wyhash.hash(seed, input)`, its one-shot form, step for
// step.
static uint64_t store_wyhash(uint64_t seed, Str in) {
    static const uint64_t secret_words[4] = {0xa0761d6478bd642full, 0xe7037ed1a0b428dbull, 0x8ebc6af09c88c6e3ull, 0x589965cc75374cc3ull};
    SLICE_TYPE(Words, const uint64_t);
    Words secret = {secret_words, 4};
    uint64_t state_words[3];
    SLICE_TYPE(State, uint64_t);
    State state = {state_words, 3};
    at(state, 0) = seed ^ store_wy_mix(seed ^ at(secret, 0), at(secret, 1));
    at(state, 1) = at(state, 0);
    at(state, 2) = at(state, 0);
    uint64_t a = 0;
    uint64_t b = 0;
    if (in.len <= 16) {
        if (in.len >= 4) {
            size_t end = in.len - 4;
            size_t quarter = (in.len >> 3) << 2;
            a = (store_wy_read(in, 0, 4) << 32) | store_wy_read(in, quarter, 4);
            b = (store_wy_read(in, end, 4) << 32) | store_wy_read(in, end - quarter, 4);
        } else if (in.len > 0) {
            a = ((uint64_t)str_byte(in, 0) << 16) | ((uint64_t)str_byte(in, in.len >> 1) << 8) | str_byte(in, in.len - 1);
        }
    } else {
        size_t i = 0;
        if (in.len >= 48) {
            while (i + 48 < in.len) {
                for (size_t r = 0; r < 3; r++) {
                    uint64_t x = store_wy_read(in, i + 16 * r, 8);
                    uint64_t y = store_wy_read(in, i + 16 * r + 8, 8);
                    at(state, r) = store_wy_mix(x ^ at(secret, r + 1), y ^ at(state, r));
                }
                i += 48;
            }
            at(state, 0) ^= at(state, 1) ^ at(state, 2);
        }
        Str rest = str_sub(in, i, in.len);
        size_t j = 0;
        while (j + 16 < rest.len) {
            at(state, 0) = store_wy_mix(store_wy_read(rest, j, 8) ^ at(secret, 1), store_wy_read(rest, j + 8, 8) ^ at(state, 0));
            j += 16;
        }
        a = store_wy_read(in, in.len - 16, 8);
        b = store_wy_read(in, in.len - 8, 8);
    }
    a ^= at(secret, 1);
    b ^= at(state, 0);
    store_wy_mum(&a, &b);
    return store_wy_mix(a ^ at(secret, 0) ^ (uint64_t)in.len, b ^ at(secret, 1));
}

// The temporary name `replace` writes beside `name`: `~`, eight lowercase
// hex digits of the low half of a Wyhash of the name in lowercase (its
// first STORE_MAX_NAME bytes), then `.tmp`. Thirteen bytes whatever the
// name, and one per name in any case.
static bool store_sibling_name(Arena *a, Str name, Str *out) {
    char lower[STORE_MAX_NAME];
    Bytes low = {lower, sizeof lower};
    size_t n = name.len < STORE_MAX_NAME ? name.len : STORE_MAX_NAME;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = str_byte(name, i);
        at(low, i) = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    uint32_t h = (uint32_t)store_wyhash(0, (Str){lower, n});
    char *p = arena_alloc(a, 13, 1);
    if (p == NULL) return false;
    Bytes t = {p, 13};
    at(t, 0) = '~';
    for (size_t k = 0; k < 8; k++) at(t, 1 + k) = "0123456789abcdef"[(h >> (28 - 4 * k)) & 0xf];
    mem_copy(&at(t, 9), ".tmp", 4);
    out->ptr = p;
    out->len = 13;
    return true;
}

// ── the store's calls ───────────────────────────────────────────────────

static StoreErr store_linux_stat(void *ctx, Arena *a, Str path, StoreStat *out) {
    (void)ctx;
    struct stat st;
    StoreErr e = store_os_stat(path, true, &st);
    if (e == STORE_FILE_NOT_FOUND) {
        bool has = false;
        Str p = path;
        StoreErr r = store_retry(a, path, &has, &p);
        if (r != STORE_OK) return r;
        if (!has) return e;
        e = store_os_stat(p, true, &st);
    }
    if (e != STORE_OK) return e;
    out->size = (uint64_t)st.st_size;
    out->kind = store_kind_of(st.st_mode);
    out->mtime_s = (int64_t)st.st_mtim.tv_sec;
    out->mtime_ns = (int64_t)st.st_mtim.tv_nsec;
    return STORE_OK;
}

// What `stat` answers, or `*found` false when nothing is there: a path not
// found, through a file, or with a name too long for any file.
static StoreErr store_linux_stat_or_null(void *ctx, Arena *a, Str path, bool *found, StoreStat *out) {
    StoreErr e = store_linux_stat(ctx, a, path, out);
    *found = e == STORE_OK;
    if (e == STORE_FILE_NOT_FOUND || e == STORE_NOT_DIR || e == STORE_NAME_TOO_LONG) return STORE_OK;
    return e;
}

// Whether there is a file or folder at `path`, in any case: "no" only for
// what is not there, any other failure the caller's.
static StoreErr store_linux_has(void *ctx, Arena *a, Str path, bool *out) {
    StoreStat st;
    return store_linux_stat_or_null(ctx, a, path, out, &st);
}

typedef struct {
    Str name;
    StoreKind kind;
} StoreEntry;

VEC_DEFINE(StoreEntries, StoreEntry)

static StoreKind store_kind_of_dirent(unsigned char t) {
    if (t == DT_REG) return STORE_KIND_FILE;
    if (t == DT_DIR) return STORE_KIND_DIRECTORY;
    return STORE_KIND_OTHER;
}

// What a folder holds, in the order the disk gives it; a folder that is not
// there holds nothing.
static StoreErr store_linux_list(void *ctx, Arena *a, Str dir, StoreEntries *out) {
    (void)ctx;
    *out = StoreEntries_make(a);
    StoreCPath cp;
    const char *c = NULL;
    StoreErr e = store_cstr(&cp, dir, &c);
    if (e != STORE_OK) return e;
    DIR *d = opendir(c);
    if (d == NULL && errno == ENOENT) {
        bool has = false;
        Str p = dir;
        e = store_retry(a, dir, &has, &p);
        if (e != STORE_OK) return e;
        if (!has) return STORE_OK;
        e = store_cstr(&cp, p, &c);
        if (e != STORE_OK) return e;
        d = opendir(c);
    }
    if (d == NULL) return store_err_of(errno);
    StoreErr got = STORE_OK;
    for (;;) {
        errno = 0;
        struct dirent *ent = readdir(d);
        if (ent == NULL) {
            if (errno != 0) got = store_err_of(errno);
            break;
        }
        Str n = {ent->d_name, strlen(ent->d_name)};
        if (str_eq(n, S(".")) || str_eq(n, S(".."))) continue;
        StoreEntry en = {S(""), store_kind_of_dirent(ent->d_type)};
        if (!arena_dup(a, n, &en.name) || StoreEntries_push(out, en) != OK) {
            got = STORE_OUT_OF_MEMORY;
            break;
        }
    }
    closedir(d);
    return got;
}

// Up to `buf.len` bytes from `offset`, into `*got`.
static StoreErr store_linux_read_at(void *ctx, Arena *a, Str path, uint64_t offset, Bytes buf, size_t *got) {
    (void)ctx;
    *got = 0;
    StoreCPath cp;
    const char *c = NULL;
    StoreErr e = store_cstr(&cp, path, &c);
    if (e != STORE_OK) return e;
    int fd = open(c, O_RDONLY | O_CLOEXEC);
    if (fd < 0 && errno == ENOENT) {
        bool has = false;
        Str p = path;
        e = store_retry(a, path, &has, &p);
        if (e != STORE_OK) return e;
        if (!has) return STORE_FILE_NOT_FOUND;
        e = store_cstr(&cp, p, &c);
        if (e != STORE_OK) return e;
        fd = open(c, O_RDONLY | O_CLOEXEC);
    }
    if (fd < 0) return store_err_of(errno);
    StoreErr r = STORE_OK;
    while (*got < buf.len) {
        Bytes room = SLICE_SUB(Bytes, buf, *got, buf.len);
        ssize_t n = pread(fd, room.ptr, room.len, (off_t)(offset + *got));
        if (n < 0) {
            if (errno == EINTR) continue;
            r = store_err_of(errno);
            break;
        }
        if (n == 0) break;
        *got += (size_t)n;
    }
    close(fd);
    return r;
}

static StoreErr store_linux_make_dir(void *ctx, Arena *a, Str path) {
    Str p = path;
    StoreErr e = store_for_make_dir(ctx, a, path, &p);
    if (e != STORE_OK) return e;
    return store_os_make_path(p);
}

// Makes `path` hold exactly `data`, so that no stop loses it: written to a
// sibling (`store_sibling_name`) with the old file's mode, then renamed
// over it. A rename refused leaves no sibling behind.
static StoreErr store_linux_replace(void *ctx, Arena *a, Str path, Str data) {
    Str p = path;
    StoreErr e = store_for_write(ctx, a, path, &p);
    if (e != STORE_OK) return e;
    Str dir = S("");
    bool has_dir = store_dirname(p, &dir);
    if (has_dir) {
        e = store_linux_make_dir(ctx, a, dir);
        if (e != STORE_OK) return e;
    }
    Str tmp = S("");
    if (!store_sibling_name(a, store_basename(p), &tmp)) return STORE_OUT_OF_MEMORY;
    if (has_dir && !store_join(a, dir, tmp, &tmp)) return STORE_OUT_OF_MEMORY;
    struct stat st;
    e = store_os_stat(p, true, &st);
    if (e == STORE_OK) {
        e = store_os_write_file(tmp, data, st.st_mode & 07777);
    } else if (e == STORE_FILE_NOT_FOUND) {
        e = store_os_write_file(tmp, data, 0666);
    }
    if (e != STORE_OK) return e;
    StoreCPath from_cp;
    StoreCPath to_cp;
    const char *from = NULL;
    const char *to = NULL;
    e = store_cstr(&from_cp, tmp, &from);
    if (e == STORE_OK) e = store_cstr(&to_cp, p, &to);
    if (e == STORE_OK && rename(from, to) != 0) e = store_err_of(errno);
    if (e != STORE_OK && from != NULL) unlink(from);
    return e;
}

// Adds `bytes` at the end of `path`, making it and its folders if needed,
// and gives its size after.
static StoreErr store_linux_append(void *ctx, Arena *a, Str path, Str bytes, uint64_t *size) {
    Str p = path;
    StoreErr e = store_for_write(ctx, a, path, &p);
    if (e != STORE_OK) return e;
    Str dir = S("");
    if (store_dirname(p, &dir)) {
        e = store_linux_make_dir(ctx, a, dir);
        if (e != STORE_OK) return e;
    }
    StoreCPath cp;
    const char *c = NULL;
    e = store_cstr(&cp, p, &c);
    if (e != STORE_OK) return e;
    int fd = open(c, O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
    if (fd < 0) return store_err_of(errno);
    struct stat st;
    StoreErr got = STORE_OK;
    if (fstat(fd, &st) != 0) got = store_err_of(errno);
    uint64_t done = 0;
    while (got == STORE_OK && done < bytes.len) {
        Str part = str_sub(bytes, done, bytes.len);
        ssize_t n = pwrite(fd, part.ptr, part.len, (off_t)((uint64_t)st.st_size + done));
        if (n < 0) {
            if (errno == EINTR) continue;
            got = store_err_of(errno);
            break;
        }
        done += (uint64_t)n;
    }
    close(fd);
    if (got == STORE_OK) *size = (uint64_t)st.st_size + bytes.len;
    return got;
}

// Removes the file at `path`; not there is done.
static StoreErr store_linux_remove(void *ctx, Arena *a, Str path) {
    (void)ctx;
    StoreErr e = store_os_delete_file(path);
    if (e != STORE_FILE_NOT_FOUND) return e;
    bool has = false;
    Str p = path;
    e = store_retry(a, path, &has, &p);
    if (e != STORE_OK || !has) return e;
    return store_os_delete_file(p);
}

// Removes `path` and everything under it, refusing a last name FAT would
// not hold: an empty or blank one joined onto a root would name the root.
static StoreErr store_linux_remove_tree(void *ctx, Arena *a, Str path) {
    (void)ctx;
    if (!store_fat_name(store_basename(path))) return STORE_BAD_NAME;
    bool has = false;
    Str p = path;
    StoreErr e = store_retry(a, path, &has, &p);
    if (e != STORE_OK) return e;
    return store_os_delete_tree(a, has ? p : path);
}

// The store as the builtins call it, zig-webbox's `store`: each call takes
// an OS path the caller has already confined, and the arena its paths and
// answers are made in.
typedef struct {
    void *ctx;
    StoreErr (*stat)(void *ctx, Arena *a, Str path, StoreStat *out);
    StoreErr (*stat_or_null)(void *ctx, Arena *a, Str path, bool *found, StoreStat *out);
    StoreErr (*has)(void *ctx, Arena *a, Str path, bool *out);
    StoreErr (*list)(void *ctx, Arena *a, Str dir, StoreEntries *out);
    StoreErr (*read_at)(void *ctx, Arena *a, Str path, uint64_t offset, Bytes buf, size_t *got);
    StoreErr (*replace)(void *ctx, Arena *a, Str path, Str data);
    StoreErr (*append)(void *ctx, Arena *a, Str path, Str bytes, uint64_t *size);
    StoreErr (*make_dir)(void *ctx, Arena *a, Str path);
    StoreErr (*remove)(void *ctx, Arena *a, Str path);
    StoreErr (*remove_tree)(void *ctx, Arena *a, Str path);
} Store;

static inline void store_linux(LinuxStore *s, Store *out) {
    Store v = {
        s, store_linux_stat, store_linux_stat_or_null, store_linux_has, store_linux_list, store_linux_read_at,
        store_linux_replace, store_linux_append, store_linux_make_dir, store_linux_remove, store_linux_remove_tree,
    };
    *out = v;
}
