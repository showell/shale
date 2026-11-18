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
// remove-tree, each given as a `Store` vtable (`store_linux`). An error is
// a `FileSystemErr`, whose name is Zig's (`file_system_err_name`), since a
// stop prints it. The file system refuses no path for where it is: the
// root's and the mounts' confinement is the caller's (path_run.c, paths.c),
// checked before any call here.
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

// A store call's failure, by the name Zig gives it: FILE_SYSTEM_OK is none.
typedef enum {
    FILE_SYSTEM_OK,
    FILE_SYSTEM_FILE_NOT_FOUND,
    FILE_SYSTEM_NOT_DIR,
    FILE_SYSTEM_IS_DIR,
    FILE_SYSTEM_NOT_FILE,
    FILE_SYSTEM_PATH_ALREADY_EXISTS,
    FILE_SYSTEM_DIR_NOT_EMPTY,
    FILE_SYSTEM_ACCESS_DENIED,
    FILE_SYSTEM_PERMISSION_DENIED,
    FILE_SYSTEM_NO_SPACE_LEFT,
    FILE_SYSTEM_SYM_LINK_LOOP,
    FILE_SYSTEM_NAME_TOO_LONG,
    FILE_SYSTEM_BAD_NAME,
    FILE_SYSTEM_BAD_PATH_NAME,
    FILE_SYSTEM_PATH_TOO_LONG,
    FILE_SYSTEM_PATH_TOO_DEEP,
    FILE_SYSTEM_READ_ONLY_FILE_SYSTEM,
    FILE_SYSTEM_FILE_BUSY,
    FILE_SYSTEM_INPUT_OUTPUT,
    FILE_SYSTEM_SYSTEM_RESOURCES,
    FILE_SYSTEM_PROCESS_FD_QUOTA_EXCEEDED,
    FILE_SYSTEM_SYSTEM_FD_QUOTA_EXCEEDED,
    FILE_SYSTEM_DISK_QUOTA,
    FILE_SYSTEM_FILE_TOO_BIG,
    FILE_SYSTEM_CROSS_DEVICE,
    FILE_SYSTEM_STREAM_TOO_LONG,
    FILE_SYSTEM_OUT_OF_MEMORY,
    FILE_SYSTEM_UNEXPECTED,
    FILE_SYSTEM_ERR_COUNT,
} FileSystemErr;

// The error's name as Zig's `@errorName` gives it. The names follow the
// enum's order.
static Str file_system_err_name(FileSystemErr e) {
    static const Str names[FILE_SYSTEM_ERR_COUNT] = {
        S(""), S("FileNotFound"), S("NotDir"), S("IsDir"), S("NotFile"), S("PathAlreadyExists"),
        S("DirNotEmpty"), S("AccessDenied"), S("PermissionDenied"), S("NoSpaceLeft"), S("SymLinkLoop"),
        S("NameTooLong"), S("BadName"), S("BadPathName"), S("PathTooLong"), S("PathTooDeep"),
        S("ReadOnlyFileSystem"), S("FileBusy"), S("InputOutput"), S("SystemResources"),
        S("ProcessFdQuotaExceeded"), S("SystemFdQuotaExceeded"), S("DiskQuota"), S("FileTooBig"),
        S("CrossDevice"), S("StreamTooLong"), S("OutOfMemory"), S("Unexpected"),
    };
    SLICE_TYPE(ErrNames, const Str);
    ErrNames n = {names, FILE_SYSTEM_ERR_COUNT};
    return at(n, (size_t)e);
}

// An errno as the error std.Io gives for it. Zig maps a few per call (an
// EINVAL), and those reach no message Shale prints, so one table serves.
static FileSystemErr file_system_err_of(int e) {
    switch (e) {
    case ENOENT: return FILE_SYSTEM_FILE_NOT_FOUND;
    case ENOTDIR: return FILE_SYSTEM_NOT_DIR;
    case EISDIR: return FILE_SYSTEM_IS_DIR;
    case EEXIST: return FILE_SYSTEM_PATH_ALREADY_EXISTS;
    case ENOTEMPTY: return FILE_SYSTEM_DIR_NOT_EMPTY;
    case EACCES: return FILE_SYSTEM_ACCESS_DENIED;
    case EPERM: return FILE_SYSTEM_PERMISSION_DENIED;
    case ENOSPC: return FILE_SYSTEM_NO_SPACE_LEFT;
    case ELOOP: return FILE_SYSTEM_SYM_LINK_LOOP;
    case ENAMETOOLONG: return FILE_SYSTEM_NAME_TOO_LONG;
    case EROFS: return FILE_SYSTEM_READ_ONLY_FILE_SYSTEM;
    case EBUSY: return FILE_SYSTEM_FILE_BUSY;
    case ETXTBSY: return FILE_SYSTEM_FILE_BUSY;
    case EIO: return FILE_SYSTEM_INPUT_OUTPUT;
    case ENOMEM: return FILE_SYSTEM_SYSTEM_RESOURCES;
    case EMFILE: return FILE_SYSTEM_PROCESS_FD_QUOTA_EXCEEDED;
    case ENFILE: return FILE_SYSTEM_SYSTEM_FD_QUOTA_EXCEEDED;
    case EDQUOT: return FILE_SYSTEM_DISK_QUOTA;
    case EFBIG: return FILE_SYSTEM_FILE_TOO_BIG;
    case EXDEV: return FILE_SYSTEM_CROSS_DEVICE;
    default: return FILE_SYSTEM_UNEXPECTED;
    }
}

// A path ended by a NUL, for the kernel, on the caller's stack: Zig's
// posix layer copies a path into a PATH_MAX buffer on its stack for each
// call. A copy in the arena outlived the call, so a run of many changes,
// each a score of calls into the kernel, held about twice the arena Zig
// did.
typedef struct {
    char bytes[PATH_MAX];
} FileSystemCPath;

// `s` ended by a NUL in `buf`, into `*out`, through slice.h's str_cstr_into
// (the DRAGON of a NUL inside is there): NameTooLong when it does not fit
// with its NUL, past PATH_MAX, and BadPathName for a NUL inside, as Zig's
// posix layer answers. `*out` lives as long as `buf`.
static FileSystemErr file_system_cstr(FileSystemCPath *buf, Str s, const char **out) {
    Bytes room = {buf->bytes, sizeof buf->bytes};
    StrCStr e = str_cstr_into(room, s);
    if (e == STR_CSTR_TOO_LONG) return FILE_SYSTEM_NAME_TOO_LONG;
    if (e == STR_CSTR_NUL) return FILE_SYSTEM_BAD_PATH_NAME;
    *out = buf->bytes;
    return FILE_SYSTEM_OK;
}

// The errno the kernel's call would have failed with on a path file_system_cstr
// refused, so its caller's own error path says it: ENAMETOOLONG past
// PATH_MAX, EINVAL for a NUL inside. Neither is an arena's refusal.
static inline int file_system_cstr_errno(FileSystemErr e) {
    return e == FILE_SYSTEM_NAME_TOO_LONG ? ENAMETOOLONG : EINVAL;
}

// ── the bare-metal host's limits ────────────────────────────────────────

// The bare-metal host's longest path, and the most parts a path has from
// the volume's root.
enum {
    FILE_SYSTEM_PATH_LEN_MAX = 256,
    FILE_SYSTEM_DEPTH_MAX = 16,
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
} FileSystem;

// Refuses a path the bare-metal host could not hold, measured as it would
// spell it: an empty part (`//` or a trailing `/`) is BadName, past
// FILE_SYSTEM_PATH_LEN_MAX bytes PathTooLong, past FILE_SYSTEM_DEPTH_MAX
// parts PathTooDeep. A path under neither base is not measured.
static FileSystemErr file_system_within_limits(const FileSystem *s, Str path) {
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
        if (empty_part) return FILE_SYSTEM_BAD_NAME;
        if (4 + rest.len > FILE_SYSTEM_PATH_LEN_MAX) return FILE_SYSTEM_PATH_TOO_LONG;
        if (depth > FILE_SYSTEM_DEPTH_MAX) return FILE_SYSTEM_PATH_TOO_DEEP;
        return FILE_SYSTEM_OK;
    }
    return FILE_SYSTEM_OK;
}

// ── finding a name in another case ──────────────────────────────────────

static bool file_system_exists(Str path) {
    FileSystemCPath cp;
    const char *c = NULL;
    if (file_system_cstr(&cp, path, &c) != FILE_SYSTEM_OK) return false;
    return access(c, F_OK) == 0;
}

// The next entry of the listing `d` that is neither `.` nor `..`, into
// `*ent` and its name into `*name`; false at the listing's end, with `*err`
// set when readdir failed and left alone when it ended.
static bool file_system_dir_next(DIR *d, struct dirent **ent, Str *name, FileSystemErr *err) {
    for (;;) {
        errno = 0;
        *ent = readdir(d);
        if (*ent == NULL) {
            if (errno != 0) *err = file_system_err_of(errno);
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
static FileSystemErr file_system_case_sibling(Arena *a, Str dir, Str name, bool *has, Str *found) {
    *has = false;
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, dir.len == 0 ? S(".") : dir, &c);
    if (e != FILE_SYSTEM_OK) return e;
    DIR *d = opendir(c);
    if (d == NULL) {
        if (errno == ENOENT || errno == ENOTDIR) return FILE_SYSTEM_OK;
        return file_system_err_of(errno);
    }
    FileSystemErr got = FILE_SYSTEM_OK;
    struct dirent *ent = NULL;
    Str n = S("");
    while (file_system_dir_next(d, &ent, &n, &got)) {
        if (path_eq_nocase(n, name)) {
            if (!arena_dup(a, n, found)) {
                got = FILE_SYSTEM_OUT_OF_MEMORY;
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
static FileSystemErr file_system_resolve_case(Arena *a, Str path, Str *out) {
    if (file_system_exists(path)) {
        *out = path;
        return FILE_SYSTEM_OK;
    }
    Str parent = S("");
    if (!path_dirname(path, &parent)) parent = S("");
    Str name = path_basename(path);
    Str dir = parent;
    if (parent.len > 0) {
        FileSystemErr e = file_system_resolve_case(a, parent, &dir);
        if (e != FILE_SYSTEM_OK) return e;
    }
    bool has = false;
    Str found = name;
    FileSystemErr e = file_system_case_sibling(a, dir, name, &has, &found);
    if (e != FILE_SYSTEM_OK) return e;
    if (!has) found = name;
    if (dir.len == 0) {
        *out = found;
        return FILE_SYSTEM_OK;
    }
    return path_join(a, dir, found, out) ? FILE_SYSTEM_OK : FILE_SYSTEM_OUT_OF_MEMORY;
}

// A lookup that missed: the same path in another case, into `*out` with
// `*has` true, or `*has` false when there is no such thing in any case.
// Zig's `retry`, which stats with `statFile` (a path through a file in
// another case is NotDir here).
static FileSystemErr file_system_retry_case(Arena *a, Str path, bool *has, Str *out) {
    *has = false;
    Str p = path;
    FileSystemErr e = file_system_resolve_case(a, path, &p);
    if (e != FILE_SYSTEM_OK) return e;
    if (str_eq(p, path)) return FILE_SYSTEM_OK;
    FileSystemCPath cp;
    const char *c = NULL;
    e = file_system_cstr(&cp, p, &c);
    if (e != FILE_SYSTEM_OK) return e;
    struct stat st;
    if (stat(c, &st) != 0) {
        if (errno == ENOENT) return FILE_SYSTEM_OK;
        return file_system_err_of(errno);
    }
    *has = true;
    *out = p;
    return FILE_SYSTEM_OK;
}

// `path` resolved for a call that may create its last part, which must be
// a name FAT holds.
static FileSystemErr file_system_for_write(const FileSystem *s, Arena *a, Str path, Str *out) {
    if (!path_strict_name(path_basename(path))) return FILE_SYSTEM_BAD_NAME;
    FileSystemErr e = file_system_within_limits(s, path);
    if (e != FILE_SYSTEM_OK) return e;
    return file_system_resolve_case(a, path, out);
}

// `path` resolved for a call that makes folders: every part past what is
// there must be a name FAT holds.
static FileSystemErr file_system_for_make_dir(const FileSystem *s, Arena *a, Str path, Str *out) {
    FileSystemErr e = file_system_within_limits(s, path);
    if (e != FILE_SYSTEM_OK) return e;
    Str p = path;
    e = file_system_resolve_case(a, path, &p);
    if (e != FILE_SYSTEM_OK) return e;
    Str rest = p;
    while (!file_system_exists(rest)) {
        if (!path_strict_name(path_basename(rest))) return FILE_SYSTEM_BAD_NAME;
        if (!path_dirname(rest, &rest)) break;
    }
    *out = p;
    return FILE_SYSTEM_OK;
}

// ── the operating system's calls, as std.Io makes them ──────────────────

typedef enum {
    FILE_SYSTEM_KIND_FILE,
    FILE_SYSTEM_KIND_DIRECTORY,
    FILE_SYSTEM_KIND_OTHER,
} FileSystemKind;

// What `stat` answers, as Zig's `Stat`: the size, the kind, and the
// modification time in whole seconds and their nanoseconds.
typedef struct {
    uint64_t size;
    FileSystemKind kind;
    int64_t mtime_s;
    int64_t mtime_ns;
} FileSystemStat;

static FileSystemKind file_system_kind_of(mode_t m) {
    if (S_ISREG(m)) return FILE_SYSTEM_KIND_FILE;
    if (S_ISDIR(m)) return FILE_SYSTEM_KIND_DIRECTORY;
    return FILE_SYSTEM_KIND_OTHER;
}

static FileSystemErr store_os_stat(Str path, bool follow, struct stat *st) {
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, path, &c);
    if (e != FILE_SYSTEM_OK) return e;
    int r = follow ? stat(c, st) : lstat(c, st);
    return r == 0 ? FILE_SYSTEM_OK : file_system_err_of(errno);
}

// Makes `path` hold `data`, created with `mode` if it is not there (the
// umask applies, as Zig's `writeFile`); a file there keeps its own mode.
static FileSystemErr store_os_write_file(Str path, Str data, mode_t mode) {
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, path, &c);
    if (e != FILE_SYSTEM_OK) return e;
    int fd = open(c, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) return file_system_err_of(errno);
    size_t done = 0;
    FileSystemErr got = FILE_SYSTEM_OK;
    while (done < data.len) {
        Str part = str_sub(data, done, data.len);
        ssize_t n = write(fd, part.ptr, part.len);
        if (n < 0) {
            if (errno == EINTR) continue;
            got = file_system_err_of(errno);
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
static FileSystemErr store_os_read_file(Arena *a, Str path, size_t limit, Str *out) {
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, path, &c);
    if (e != FILE_SYSTEM_OK) return e;
    int fd = open(c, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return file_system_err_of(errno);
    Buf b = buf_make(a);
    FileSystemErr got = FILE_SYSTEM_OK;
    char chunk[16384];
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n < 0) {
            if (errno == EINTR) continue;
            got = file_system_err_of(errno);
            break;
        }
        if (n == 0) break;
        if ((size_t)n > limit - b.len) {
            got = FILE_SYSTEM_STREAM_TOO_LONG;
            break;
        }
        if (buf_str(&b, (Str){chunk, (size_t)n}) != OK) {
            got = FILE_SYSTEM_OUT_OF_MEMORY;
            break;
        }
    }
    close(fd);
    if (got == FILE_SYSTEM_OK) *out = buf_view(&b);
    return got;
}

// Removes the file at `path`: IsDir for a folder, as Linux's `unlink`
// answers it.
static FileSystemErr store_os_delete_file(Str path) {
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, path, &c);
    if (e != FILE_SYSTEM_OK) return e;
    return unlink(c) == 0 ? FILE_SYSTEM_OK : file_system_err_of(errno);
}

// The folder `path` opened to be listed, following no link, into `*fd`.
// Its own function, so the recursion of store_os_delete_tree holds no path
// buffer of its own a level.
static FileSystemErr store_os_open_dir(Str path, int *fd) {
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, path, &c);
    if (e != FILE_SYSTEM_OK) return e;
    // DRAGON: the store's boundaries. A folder swapped for a link to another
    // tree between store_os_delete_tree's unlink and this open would be
    // entered and emptied.
    // REMEDY: O_NOFOLLOW on the folder; a link met inside is unlinked as a
    // file by the recursion's first step, never followed.
    *fd = open(c, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    return *fd < 0 ? file_system_err_of(errno) : FILE_SYSTEM_OK;
}

// Removes the empty folder `path`.
static FileSystemErr store_os_remove_dir(Str path) {
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, path, &c);
    if (e != FILE_SYSTEM_OK) return e;
    return rmdir(c) == 0 ? FILE_SYSTEM_OK : file_system_err_of(errno);
}

// Removes `path` and everything under it, following no link (a link is
// removed, never entered); nothing there is done.
static FileSystemErr store_os_delete_tree(Arena *a, Str path) {
    FileSystemErr e = store_os_delete_file(path);
    if (e == FILE_SYSTEM_OK || e == FILE_SYSTEM_FILE_NOT_FOUND) return FILE_SYSTEM_OK;
    if (e != FILE_SYSTEM_IS_DIR) return e;
    int fd = -1;
    e = store_os_open_dir(path, &fd);
    if (e != FILE_SYSTEM_OK) return e;
    DIR *d = fdopendir(fd);
    if (d == NULL) {
        close(fd);
        return file_system_err_of(errno);
    }
    FileSystemErr got = FILE_SYSTEM_OK;
    struct dirent *ent = NULL;
    Str n = S("");
    while (file_system_dir_next(d, &ent, &n, &got)) {
        Str child = S("");
        if (!path_join(a, path, n, &child)) {
            got = FILE_SYSTEM_OUT_OF_MEMORY;
            break;
        }
        got = store_os_delete_tree(a, child);
        if (got != FILE_SYSTEM_OK) break;
    }
    closedir(d);
    if (got != FILE_SYSTEM_OK) return got;
    return store_os_remove_dir(path);
}

// Makes the one folder `path`: done when a folder is there already, NotDir
// for a file in the way, and `*missing` true when its parent is not there.
// Its own function, so the recursion of store_os_make_path holds no path
// buffer of its own a level.
static FileSystemErr store_os_make_dir_one(Str path, bool *missing) {
    *missing = false;
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, path, &c);
    if (e != FILE_SYSTEM_OK) return e;
    if (mkdir(c, 0777) == 0) return FILE_SYSTEM_OK;
    int err = errno;
    if (err == EEXIST) {
        struct stat st;
        if (stat(c, &st) != 0) return file_system_err_of(errno);
        return S_ISDIR(st.st_mode) ? FILE_SYSTEM_OK : FILE_SYSTEM_NOT_DIR;
    }
    *missing = err == ENOENT;
    return file_system_err_of(err);
}

// Makes the folder `path` and every folder above it that is missing; a
// file in the way is NotDir.
static FileSystemErr store_os_make_path(Str path) {
    bool missing = false;
    FileSystemErr e = store_os_make_dir_one(path, &missing);
    if (!missing) return e;
    Str parent = S("");
    if (!path_dirname(path, &parent)) return FILE_SYSTEM_FILE_NOT_FOUND;
    e = store_os_make_path(parent);
    if (e != FILE_SYSTEM_OK) return e;
    return store_os_make_dir_one(path, &missing);
}

// ── the store's calls ───────────────────────────────────────────────────

static FileSystemErr store_linux_stat(void *ctx, Arena *a, Str path, FileSystemStat *out) {
    (void)ctx;
    struct stat st;
    FileSystemErr e = store_os_stat(path, true, &st);
    if (e == FILE_SYSTEM_FILE_NOT_FOUND) {
        bool has = false;
        Str p = path;
        FileSystemErr r = file_system_retry_case(a, path, &has, &p);
        if (r != FILE_SYSTEM_OK) return r;
        if (!has) return e;
        e = store_os_stat(p, true, &st);
    }
    if (e != FILE_SYSTEM_OK) return e;
    out->size = (uint64_t)st.st_size;
    out->kind = file_system_kind_of(st.st_mode);
    out->mtime_s = (int64_t)st.st_mtim.tv_sec;
    out->mtime_ns = (int64_t)st.st_mtim.tv_nsec;
    return FILE_SYSTEM_OK;
}

// What `stat` answers, or `*found` false when nothing is there: a path not
// found, through a file, or with a name too long for any file.
static FileSystemErr store_linux_stat_or_null(void *ctx, Arena *a, Str path, bool *found, FileSystemStat *out) {
    FileSystemErr e = store_linux_stat(ctx, a, path, out);
    *found = e == FILE_SYSTEM_OK;
    if (e == FILE_SYSTEM_FILE_NOT_FOUND || e == FILE_SYSTEM_NOT_DIR || e == FILE_SYSTEM_NAME_TOO_LONG) return FILE_SYSTEM_OK;
    return e;
}

// Whether there is a file or folder at `path`, in any case: "no" only for
// what is not there, any other failure the caller's.
static FileSystemErr store_linux_has(void *ctx, Arena *a, Str path, bool *out) {
    FileSystemStat st;
    return store_linux_stat_or_null(ctx, a, path, out, &st);
}

typedef struct {
    Str name;
    FileSystemKind kind;
} FileSystemEntry;

VEC_DEFINE(FileSystemEntries, FileSystemEntry)

static FileSystemKind file_system_kind_of_dirent(unsigned char t) {
    if (t == DT_REG) return FILE_SYSTEM_KIND_FILE;
    if (t == DT_DIR) return FILE_SYSTEM_KIND_DIRECTORY;
    return FILE_SYSTEM_KIND_OTHER;
}

// What a folder holds, in the order the disk gives it; a folder that is not
// there holds nothing.
static FileSystemErr store_linux_list(void *ctx, Arena *a, Str dir, FileSystemEntries *out) {
    (void)ctx;
    *out = FileSystemEntries_make(a);
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, dir, &c);
    if (e != FILE_SYSTEM_OK) return e;
    DIR *d = opendir(c);
    if (d == NULL && errno == ENOENT) {
        bool has = false;
        Str p = dir;
        e = file_system_retry_case(a, dir, &has, &p);
        if (e != FILE_SYSTEM_OK) return e;
        if (!has) return FILE_SYSTEM_OK;
        e = file_system_cstr(&cp, p, &c);
        if (e != FILE_SYSTEM_OK) return e;
        d = opendir(c);
    }
    if (d == NULL) return file_system_err_of(errno);
    FileSystemErr got = FILE_SYSTEM_OK;
    struct dirent *ent = NULL;
    Str n = S("");
    while (file_system_dir_next(d, &ent, &n, &got)) {
        FileSystemEntry en = {S(""), file_system_kind_of_dirent(ent->d_type)};
        if (!arena_dup(a, n, &en.name) || FileSystemEntries_push(out, en) != OK) {
            got = FILE_SYSTEM_OUT_OF_MEMORY;
            break;
        }
    }
    closedir(d);
    return got;
}

// Up to `buf.len` bytes from `offset`, into `*got`.
static FileSystemErr store_linux_read_at(void *ctx, Arena *a, Str path, uint64_t offset, Bytes buf, size_t *got) {
    (void)ctx;
    *got = 0;
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, path, &c);
    if (e != FILE_SYSTEM_OK) return e;
    int fd = open(c, O_RDONLY | O_CLOEXEC);
    if (fd < 0 && errno == ENOENT) {
        bool has = false;
        Str p = path;
        e = file_system_retry_case(a, path, &has, &p);
        if (e != FILE_SYSTEM_OK) return e;
        if (!has) return FILE_SYSTEM_FILE_NOT_FOUND;
        e = file_system_cstr(&cp, p, &c);
        if (e != FILE_SYSTEM_OK) return e;
        fd = open(c, O_RDONLY | O_CLOEXEC);
    }
    if (fd < 0) return file_system_err_of(errno);
    FileSystemErr r = FILE_SYSTEM_OK;
    while (*got < buf.len) {
        Bytes room = SLICE_SUB(Bytes, buf, *got, buf.len);
        ssize_t n = pread(fd, room.ptr, room.len, (off_t)(offset + *got));
        if (n < 0) {
            if (errno == EINTR) continue;
            r = file_system_err_of(errno);
            break;
        }
        if (n == 0) break;
        *got += (size_t)n;
    }
    close(fd);
    return r;
}

static FileSystemErr store_linux_make_dir(void *ctx, Arena *a, Str path) {
    Str p = path;
    FileSystemErr e = file_system_for_make_dir(ctx, a, path, &p);
    if (e != FILE_SYSTEM_OK) return e;
    return store_os_make_path(p);
}

// Makes `path` hold exactly `data`, so that no stop loses it: written to a
// sibling (`path_temp_name`) with the old file's mode, then renamed
// over it. A rename refused leaves no sibling behind.
static FileSystemErr store_linux_replace(void *ctx, Arena *a, Str path, Str data) {
    Str p = path;
    FileSystemErr e = file_system_for_write(ctx, a, path, &p);
    if (e != FILE_SYSTEM_OK) return e;
    Str dir = S("");
    bool has_dir = path_dirname(p, &dir);
    if (has_dir) {
        e = store_linux_make_dir(ctx, a, dir);
        if (e != FILE_SYSTEM_OK) return e;
    }
    Str tmp = S("");
    if (!path_temp_name(a, path_basename(p), &tmp)) return FILE_SYSTEM_OUT_OF_MEMORY;
    if (has_dir && !path_join(a, dir, tmp, &tmp)) return FILE_SYSTEM_OUT_OF_MEMORY;
    struct stat st;
    e = store_os_stat(p, true, &st);
    if (e == FILE_SYSTEM_OK) {
        e = store_os_write_file(tmp, data, st.st_mode & 07777);
    } else if (e == FILE_SYSTEM_FILE_NOT_FOUND) {
        e = store_os_write_file(tmp, data, 0666);
    }
    if (e != FILE_SYSTEM_OK) return e;
    FileSystemCPath from_cp;
    FileSystemCPath to_cp;
    const char *from = NULL;
    const char *to = NULL;
    e = file_system_cstr(&from_cp, tmp, &from);
    if (e == FILE_SYSTEM_OK) e = file_system_cstr(&to_cp, p, &to);
    if (e == FILE_SYSTEM_OK && rename(from, to) != 0) e = file_system_err_of(errno);
    if (e != FILE_SYSTEM_OK && from != NULL) unlink(from);
    return e;
}

// Adds `bytes` at the end of `path`, making it and its folders if needed,
// and gives its size after.
static FileSystemErr store_linux_append(void *ctx, Arena *a, Str path, Str bytes, uint64_t *size) {
    Str p = path;
    FileSystemErr e = file_system_for_write(ctx, a, path, &p);
    if (e != FILE_SYSTEM_OK) return e;
    Str dir = S("");
    if (path_dirname(p, &dir)) {
        e = store_linux_make_dir(ctx, a, dir);
        if (e != FILE_SYSTEM_OK) return e;
    }
    FileSystemCPath cp;
    const char *c = NULL;
    e = file_system_cstr(&cp, p, &c);
    if (e != FILE_SYSTEM_OK) return e;
    int fd = open(c, O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
    if (fd < 0) return file_system_err_of(errno);
    struct stat st;
    FileSystemErr got = FILE_SYSTEM_OK;
    if (fstat(fd, &st) != 0) got = file_system_err_of(errno);
    uint64_t done = 0;
    while (got == FILE_SYSTEM_OK && done < bytes.len) {
        Str part = str_sub(bytes, done, bytes.len);
        ssize_t n = pwrite(fd, part.ptr, part.len, (off_t)((uint64_t)st.st_size + done));
        if (n < 0) {
            if (errno == EINTR) continue;
            got = file_system_err_of(errno);
            break;
        }
        done += (uint64_t)n;
    }
    close(fd);
    if (got == FILE_SYSTEM_OK) *size = (uint64_t)st.st_size + bytes.len;
    return got;
}

// Removes the file at `path`; not there is done.
static FileSystemErr store_linux_remove(void *ctx, Arena *a, Str path) {
    (void)ctx;
    FileSystemErr e = store_os_delete_file(path);
    if (e != FILE_SYSTEM_FILE_NOT_FOUND) return e;
    bool has = false;
    Str p = path;
    e = file_system_retry_case(a, path, &has, &p);
    if (e != FILE_SYSTEM_OK || !has) return e;
    return store_os_delete_file(p);
}

// Removes `path` and everything under it, refusing a last name FAT would
// not hold: an empty or blank one joined onto a root would name the root.
static FileSystemErr store_linux_remove_tree(void *ctx, Arena *a, Str path) {
    (void)ctx;
    if (!path_strict_name(path_basename(path))) return FILE_SYSTEM_BAD_NAME;
    bool has = false;
    Str p = path;
    FileSystemErr e = file_system_retry_case(a, path, &has, &p);
    if (e != FILE_SYSTEM_OK) return e;
    return store_os_delete_tree(a, has ? p : path);
}

// The store as the builtins call it, zig-webbox's `store`: each call takes
// an OS path the caller has already confined, and the arena its paths and
// answers are made in.
typedef struct {
    void *ctx;
    FileSystemErr (*stat)(void *ctx, Arena *a, Str path, FileSystemStat *out);
    FileSystemErr (*stat_or_null)(void *ctx, Arena *a, Str path, bool *found, FileSystemStat *out);
    FileSystemErr (*has)(void *ctx, Arena *a, Str path, bool *out);
    FileSystemErr (*list)(void *ctx, Arena *a, Str dir, FileSystemEntries *out);
    FileSystemErr (*read_at)(void *ctx, Arena *a, Str path, uint64_t offset, Bytes buf, size_t *got);
    FileSystemErr (*replace)(void *ctx, Arena *a, Str path, Str data);
    FileSystemErr (*append)(void *ctx, Arena *a, Str path, Str bytes, uint64_t *size);
    FileSystemErr (*make_dir)(void *ctx, Arena *a, Str path);
    FileSystemErr (*remove)(void *ctx, Arena *a, Str path);
    FileSystemErr (*remove_tree)(void *ctx, Arena *a, Str path);
} Store;

static inline void store_linux(FileSystem *s, Store *out) {
    Store v = {
        s, store_linux_stat, store_linux_stat_or_null, store_linux_has, store_linux_list, store_linux_read_at,
        store_linux_replace, store_linux_append, store_linux_make_dir, store_linux_remove, store_linux_remove_tree,
    };
    *out = v;
}
