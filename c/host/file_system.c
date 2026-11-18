// file_system.c: the calls into the file system, every file a script reads
// or changes, held to Shale's rules: a strict name for what it makes
// (path.c's `path_strict_name`), case that does not tell two names apart (a
// missing name is looked for in another case, and a new one keeps the case
// it was given), and, under the host's two bases, the bare-metal host's
// limits on a path's length and depth. The path algebra is path.c's, which
// makes no call.
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
// `openat`'s and `realpath`'s declarations, and path.c before this.

#ifndef _GNU_SOURCE
#error "file_system.c needs _GNU_SOURCE defined before the first include: realpath, openat, fdopendir"
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

// A path ended by a NUL, for the kernel, on the caller's stack: Zig's
// posix layer copies a path into a PATH_MAX buffer on its stack for each
// call. A copy in the arena outlived the call, so a run of many changes,
// each a score of calls into the kernel, held about twice the arena Zig
// did.
typedef struct {
    char bytes[PATH_MAX];
} StoreCPath;

// `s` ended by a NUL in `buf`, into `*out`, through slice.h's str_cstr_into
// (the DRAGON of a NUL inside is there): NameTooLong when it does not fit
// with its NUL, past PATH_MAX, and BadPathName for a NUL inside, as Zig's
// posix layer answers. `*out` lives as long as `buf`.
static StoreErr store_cstr(StoreCPath *buf, Str s, const char **out) {
    Bytes room = {buf->bytes, sizeof buf->bytes};
    StrCStr e = str_cstr_into(room, s);
    if (e == STR_CSTR_TOO_LONG) return STORE_NAME_TOO_LONG;
    if (e == STR_CSTR_NUL) return STORE_BAD_PATH_NAME;
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

// The bare-metal host's longest path, and the most parts a path has from
// the volume's root.
enum {
    STORE_MAX_PATH = 256,
    STORE_MAX_DEPTH = 16,
};

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

// The next entry of the listing `d` that is neither `.` nor `..`, into
// `*ent` and its name into `*name`; false at the listing's end, with `*err`
// set when readdir failed and left alone when it ended.
static bool store_dir_next(DIR *d, struct dirent **ent, Str *name, StoreErr *err) {
    for (;;) {
        errno = 0;
        *ent = readdir(d);
        if (*ent == NULL) {
            if (errno != 0) *err = store_err_of(errno);
            return false;
        }
        Str n = {(*ent)->d_name, strlen((*ent)->d_name)};
        if (str_eq(n, S(".")) || str_eq(n, S(".."))) continue;
        *name = n;
        return true;
    }
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
    struct dirent *ent = NULL;
    Str n = S("");
    while (store_dir_next(d, &ent, &n, &got)) {
        if (path_eq_nocase(n, name)) {
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
    if (!path_dirname(path, &parent)) parent = S("");
    Str name = path_basename(path);
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
    return path_join(a, dir, found, out) ? STORE_OK : STORE_OUT_OF_MEMORY;
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
    if (!path_strict_name(path_basename(path))) return STORE_BAD_NAME;
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
        if (!path_strict_name(path_basename(rest))) return STORE_BAD_NAME;
        if (!path_dirname(rest, &rest)) break;
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
    struct dirent *ent = NULL;
    Str n = S("");
    while (store_dir_next(d, &ent, &n, &got)) {
        Str child = S("");
        if (!path_join(a, path, n, &child)) {
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
    if (!path_dirname(path, &parent)) return STORE_FILE_NOT_FOUND;
    e = store_os_make_path(parent);
    if (e != STORE_OK) return e;
    return store_os_make_dir_one(path, &missing);
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
    struct dirent *ent = NULL;
    Str n = S("");
    while (store_dir_next(d, &ent, &n, &got)) {
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
// sibling (`path_temp_name`) with the old file's mode, then renamed
// over it. A rename refused leaves no sibling behind.
static StoreErr store_linux_replace(void *ctx, Arena *a, Str path, Str data) {
    Str p = path;
    StoreErr e = store_for_write(ctx, a, path, &p);
    if (e != STORE_OK) return e;
    Str dir = S("");
    bool has_dir = path_dirname(p, &dir);
    if (has_dir) {
        e = store_linux_make_dir(ctx, a, dir);
        if (e != STORE_OK) return e;
    }
    Str tmp = S("");
    if (!path_temp_name(a, path_basename(p), &tmp)) return STORE_OUT_OF_MEMORY;
    if (has_dir && !path_join(a, dir, tmp, &tmp)) return STORE_OUT_OF_MEMORY;
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
    if (path_dirname(p, &dir)) {
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
    if (!path_strict_name(path_basename(path))) return STORE_BAD_NAME;
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
