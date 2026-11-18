// file_system.c: the calls into the file system, every file a script reads
// or changes, held to Shale's rules: a strict name for what it makes
// (path.h's `path_strict_name`) and, under the host's two bases, the
// bare-metal host's limits on a path's length and depth. A name is found as
// it is spelled: one in another case is another name, and a miss is not
// found. The path algebra is path.h's, which makes no call. Every path is
// an OsStr (slice.h), so none holds a NUL.
//
// Two kinds of call. Those a script's path is given to are the ones
// Shale's builtins make, each taking the run's `FileSystem`: stat,
// stat-or-null, has, list, read-at, replace (never torn), append, make-dir,
// remove and remove-tree, held to the rules above. The exact calls the host
// makes on a path of its own (`file_system_stat_exact`, `_read_file`,
// `_write_file`, `_delete_file`, `_delete_tree`, `_make_path`) take the path
// as it is, with no rule. An error is a `FileSystemErr`,
// whose name is Zig's (`file_system_err_name`), since a stop prints it. The
// file system refuses no path for where it is: the root's and the mounts'
// confinement is the caller's (path_run.c, paths.c), checked before any
// call here.
//
// The including file defines _GNU_SOURCE before its first include, for
// `openat`'s and `realpath`'s declarations.

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
#include "path.h"

// A file system call's failure, by the name Zig gives it: FILE_SYSTEM_OK is
// none.
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
        S("NameTooLong"), S("BadName"), S("PathTooLong"), S("PathTooDeep"),
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

// `s` ended by a NUL in `buf`, into `*out`, through slice.h's str_cstr_into:
// NameTooLong, its one refusal, when it does not fit with its NUL, past
// PATH_MAX, as Zig's posix layer answers. `*out` lives as long as `buf`.
static FileSystemErr file_system_cstr(FileSystemCPath *buf, OsStr s, const char **out) {
    Bytes room = {buf->bytes, sizeof buf->bytes};
    if (!str_cstr_into(room, s)) return FILE_SYSTEM_NAME_TOO_LONG;
    *out = buf->bytes;
    return FILE_SYSTEM_OK;
}

// The part of an OsStr from `from` to `to`, an OsStr: a part of a string
// with no NUL has none.
static OsStr file_system_part(OsStr s, size_t from, size_t to) {
    return os_str_own(str_sub(s.s, from, to));
}

// `p`'s folder (path.h's path_dirname, always a part from its start), an
// OsStr; false for none.
static bool file_system_dirname(OsStr p, OsStr *out) {
    Str d = S("");
    if (!path_dirname(p.s, &d)) return false;
    *out = file_system_part(p, 0, d.len);
    return true;
}

// `dir` and `name` joined (path.h's path_join), `name` an OsStr's part or
// a name the kernel gave; false when the arena refuses.
static bool file_system_join(Arena *a, OsStr dir, Str name, OsStr *out) {
    Str j = S("");
    if (!path_join(a, dir.s, name, &j)) return false;
    *out = os_str_own(j);
    return true;
}

// ── the bare-metal host's limits ────────────────────────────────────────

// The bare-metal host's longest path, and the most parts a path has from
// the volume's root.
enum {
    FILE_SYSTEM_PATH_LEN_MAX = 256,
    FILE_SYSTEM_DEPTH_MAX = 16,
};

// The two roots as the bare-metal host spells them, `data` and `auth`: a
// path under one is measured as that host would hold it. c/shale.c sets
// `data` to the run's root and `auth` to `<root>/../auth`; a fixture that
// sets neither measures nothing.
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

// ── the rules of a path a call may make ─────────────────────────────────

// Whether anything is at `path`; a path too long for the kernel is not.
static bool file_system_exists(OsStr path) {
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

// Checks `path` for a call that may create its last part, which must be
// a strict name.
static FileSystemErr file_system_for_write(const FileSystem *s, OsStr path) {
    if (!path_strict_name(path_basename(path.s))) return FILE_SYSTEM_BAD_NAME;
    return file_system_within_limits(s, path.s);
}

// Checks `path` for a call that makes folders: every part past what is
// there must be a strict name.
static FileSystemErr file_system_for_make_dir(const FileSystem *s, OsStr path) {
    FileSystemErr e = file_system_within_limits(s, path.s);
    if (e != FILE_SYSTEM_OK) return e;
    OsStr rest = path;
    while (!file_system_exists(rest)) {
        if (!path_strict_name(path_basename(rest.s))) return FILE_SYSTEM_BAD_NAME;
        if (!file_system_dirname(rest, &rest)) break;
    }
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

static FileSystemErr file_system_stat_exact(OsStr path, bool follow, struct stat *st) {
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, path, &c);
    if (e != FILE_SYSTEM_OK) return e;
    int r = follow ? stat(c, st) : lstat(c, st);
    return r == 0 ? FILE_SYSTEM_OK : file_system_err_of(errno);
}

// Makes `path` hold `data`, created with `mode` if it is not there (the
// umask applies, as Zig's `writeFile`); a file there keeps its own mode.
static FileSystemErr file_system_write_file(OsStr path, Str data, mode_t mode) {
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
static FileSystemErr file_system_read_file(Arena *a, OsStr path, size_t limit, Str *out) {
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
static FileSystemErr file_system_delete_file(OsStr path) {
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, path, &c);
    if (e != FILE_SYSTEM_OK) return e;
    return unlink(c) == 0 ? FILE_SYSTEM_OK : file_system_err_of(errno);
}

// The folder `path` opened to be listed, following no link, into `*fd`.
// Its own function, so the recursion of file_system_delete_tree holds no path
// buffer of its own a level.
static FileSystemErr file_system_open_dir(OsStr path, int *fd) {
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, path, &c);
    if (e != FILE_SYSTEM_OK) return e;
    // DRAGON: the file system's boundaries. A folder swapped for a link to
    // another tree between file_system_delete_tree's unlink and this open
    // would be entered and emptied.
    // REMEDY: O_NOFOLLOW on the folder; a link met inside is unlinked as a
    // file by the recursion's first step, never followed.
    *fd = open(c, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    return *fd < 0 ? file_system_err_of(errno) : FILE_SYSTEM_OK;
}

// Removes the empty folder `path`.
static FileSystemErr file_system_remove_dir(OsStr path) {
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, path, &c);
    if (e != FILE_SYSTEM_OK) return e;
    return rmdir(c) == 0 ? FILE_SYSTEM_OK : file_system_err_of(errno);
}

// Removes `path` and everything under it, following no link (a link is
// removed, never entered); nothing there is done.
static FileSystemErr file_system_delete_tree(Arena *a, OsStr path) {
    FileSystemErr e = file_system_delete_file(path);
    if (e == FILE_SYSTEM_OK || e == FILE_SYSTEM_FILE_NOT_FOUND) return FILE_SYSTEM_OK;
    if (e != FILE_SYSTEM_IS_DIR) return e;
    int fd = -1;
    e = file_system_open_dir(path, &fd);
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
        OsStr child = path;
        if (!file_system_join(a, path, n, &child)) {
            got = FILE_SYSTEM_OUT_OF_MEMORY;
            break;
        }
        got = file_system_delete_tree(a, child);
        if (got != FILE_SYSTEM_OK) break;
    }
    closedir(d);
    if (got != FILE_SYSTEM_OK) return got;
    return file_system_remove_dir(path);
}

// Makes the one folder `path`: done when a folder is there already, NotDir
// for a file in the way, and `*missing` true when its parent is not there.
// Its own function, so the recursion of file_system_make_path holds no path
// buffer of its own a level.
static FileSystemErr file_system_make_dir_one(OsStr path, bool *missing) {
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
static FileSystemErr file_system_make_path(OsStr path) {
    bool missing = false;
    FileSystemErr e = file_system_make_dir_one(path, &missing);
    if (!missing) return e;
    OsStr parent = path;
    if (!file_system_dirname(path, &parent)) return FILE_SYSTEM_FILE_NOT_FOUND;
    e = file_system_make_path(parent);
    if (e != FILE_SYSTEM_OK) return e;
    return file_system_make_dir_one(path, &missing);
}

// ── the calls a script's path is given to ───────────────────────────────

static inline FileSystemErr file_system_stat(const FileSystem *fs, OsStr path, FileSystemStat *out) {
    (void)fs;
    struct stat st;
    FileSystemErr e = file_system_stat_exact(path, true, &st);
    if (e != FILE_SYSTEM_OK) return e;
    out->size = (uint64_t)st.st_size;
    out->kind = file_system_kind_of(st.st_mode);
    out->mtime_s = (int64_t)st.st_mtim.tv_sec;
    out->mtime_ns = (int64_t)st.st_mtim.tv_nsec;
    return FILE_SYSTEM_OK;
}

// What `stat` answers, or `*found` false when nothing is there: a path not
// found, through a file, or with a name too long for any file.
static inline FileSystemErr file_system_stat_or_null(const FileSystem *fs, OsStr path, bool *found, FileSystemStat *out) {
    FileSystemErr e = file_system_stat(fs, path, out);
    *found = e == FILE_SYSTEM_OK;
    if (e == FILE_SYSTEM_FILE_NOT_FOUND || e == FILE_SYSTEM_NOT_DIR || e == FILE_SYSTEM_NAME_TOO_LONG) return FILE_SYSTEM_OK;
    return e;
}

// Whether there is a file or folder at `path`: "no" only for what is not
// there, any other failure the caller's.
static inline FileSystemErr file_system_has(const FileSystem *fs, OsStr path, bool *out) {
    FileSystemStat st;
    return file_system_stat_or_null(fs, path, out, &st);
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
static inline FileSystemErr file_system_list(const FileSystem *fs, Arena *a, OsStr dir, FileSystemEntries *out) {
    (void)fs;
    *out = FileSystemEntries_make(a);
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, dir, &c);
    if (e != FILE_SYSTEM_OK) return e;
    DIR *d = opendir(c);
    if (d == NULL && errno == ENOENT) return FILE_SYSTEM_OK; // skip: every caller stats the folder first
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
static inline FileSystemErr file_system_read_at(const FileSystem *fs, OsStr path, uint64_t offset, Bytes buf, size_t *got) {
    (void)fs;
    *got = 0;
    FileSystemCPath cp;
    const char *c = NULL;
    FileSystemErr e = file_system_cstr(&cp, path, &c);
    if (e != FILE_SYSTEM_OK) return e;
    int fd = open(c, O_RDONLY | O_CLOEXEC);
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

static inline FileSystemErr file_system_make_dir(const FileSystem *fs, OsStr path) {
    FileSystemErr e = file_system_for_make_dir(fs, path);
    if (e != FILE_SYSTEM_OK) return e;
    return file_system_make_path(path);
}

// Makes `path` hold exactly `data`, so that no stop loses it: written to a
// sibling (`path_temp_name`) with the old file's mode, then renamed
// over it. A rename refused leaves no sibling behind.
static inline FileSystemErr file_system_replace(const FileSystem *fs, Arena *a, OsStr path, Str data) {
    FileSystemErr e = file_system_for_write(fs, path);
    if (e != FILE_SYSTEM_OK) return e;
    OsStr dir = path;
    bool has_dir = file_system_dirname(path, &dir);
    if (has_dir) {
        e = file_system_make_dir(fs, dir);
        if (e != FILE_SYSTEM_OK) return e;
    }
    Str name = S("");
    if (!path_temp_name(a, path_basename(path.s), &name)) return FILE_SYSTEM_OUT_OF_MEMORY;
    OsStr tmp = os_str_own(name);
    if (has_dir && !file_system_join(a, dir, name, &tmp)) return FILE_SYSTEM_OUT_OF_MEMORY;
    struct stat st;
    e = file_system_stat_exact(path, true, &st);
    if (e == FILE_SYSTEM_OK) {
        e = file_system_write_file(tmp, data, st.st_mode & 07777);
    } else if (e == FILE_SYSTEM_FILE_NOT_FOUND) {
        e = file_system_write_file(tmp, data, 0666);
    }
    if (e != FILE_SYSTEM_OK) return e;
    FileSystemCPath from_cp;
    FileSystemCPath to_cp;
    const char *from = NULL;
    const char *to = NULL;
    e = file_system_cstr(&from_cp, tmp, &from);
    if (e == FILE_SYSTEM_OK) e = file_system_cstr(&to_cp, path, &to);
    if (e == FILE_SYSTEM_OK && rename(from, to) != 0) e = file_system_err_of(errno);
    if (e != FILE_SYSTEM_OK && from != NULL) (void)unlink(from); // ignored: the rename's error is the answer
    return e;
}

// Adds `bytes` at the end of `path`, making it and its folders if needed,
// and gives its size after.
static inline FileSystemErr file_system_append(const FileSystem *fs, OsStr path, Str bytes, uint64_t *size) {
    FileSystemErr e = file_system_for_write(fs, path);
    if (e != FILE_SYSTEM_OK) return e;
    OsStr dir = path;
    if (file_system_dirname(path, &dir)) {
        e = file_system_make_dir(fs, dir);
        if (e != FILE_SYSTEM_OK) return e;
    }
    FileSystemCPath cp;
    const char *c = NULL;
    e = file_system_cstr(&cp, path, &c);
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
static inline FileSystemErr file_system_remove(const FileSystem *fs, OsStr path) {
    (void)fs;
    FileSystemErr e = file_system_delete_file(path);
    return e == FILE_SYSTEM_FILE_NOT_FOUND ? FILE_SYSTEM_OK : e;
}

// Removes `path` and everything under it, refusing a last name that is not
// strict: an empty or blank one joined onto a root would name the root.
static inline FileSystemErr file_system_remove_tree(const FileSystem *fs, Arena *a, OsStr path) {
    (void)fs;
    if (!path_strict_name(path_basename(path.s))) return FILE_SYSTEM_BAD_NAME;
    return file_system_delete_tree(a, path);
}
