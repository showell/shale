// swap_file.c: a file written whole beside its target and renamed over it,
// so a reader sees the old file or the new one and never a part. The
// temporary file is made mode 0600 beside the target, synced, and renamed
// into place. `run`'s `stdout_secret` (host_run.c) stores a secret this way,
// and so do `shale --secrets`'s store and deny (secrets_mode.c).
//
// Include file_system.c first.

#include <sys/syscall.h>

// A swap in progress: the target's and the temporary file's paths, each
// NUL-ended in a buffer of its own (so a swap holds no pointer into itself
// and may be copied), and the temporary file's descriptor.
typedef struct {
    FileSystemCPath path;
    FileSystemCPath tmp;
    int fd;
    // Whether the temporary file is there to remove.
    bool made;
} SwapFile;

// What is at a swap's target before it is written.
typedef enum {
    SWAP_ABSENT,
    SWAP_FILE,
    // A folder, a link, a FIFO or a device: never replaced.
    SWAP_NOT_FILE,
    // `lstat` failed for another reason, its errno in `*err`.
    SWAP_UNSEEN,
} SwapThere;

// swap_file_open's answer when its arena refused: no errno is negative.
enum { SWAP_NO_ROOM = -1 };

// How a swap's last step ended.
typedef enum {
    SWAP_DONE,
    // fsync failed: nothing is stored.
    SWAP_UNSYNCED,
    // A file was made at the target meanwhile and `replace` was false.
    SWAP_TAKEN,
    // The rename failed, its errno in `*err`.
    SWAP_UNRENAMED,
} SwapEnd;

// Readies `s` for the target `path`, an absolute path: FILE_SYSTEM_OK, or
// PathTooLong for a path past PATH_MAX.
static FileSystemErr swap_file_target(SwapFile *s, OsStr path) {
    memset(s, 0, sizeof *s);
    s->fd = -1;
    const char *c = NULL; // cstr: NUL-ended for the kernel
    return file_system_cstr(&s->path, path, &c);
}

// What is at the target now, without following a link there.
static SwapThere swap_file_there(const SwapFile *s, int *err) {
    struct stat st;
    *err = 0;
    if (lstat(s->path.bytes, &st) == 0) return S_ISREG(st.st_mode) ? SWAP_FILE : SWAP_NOT_FILE;
    if (errno == ENOENT) return SWAP_ABSENT;
    *err = errno;
    return SWAP_UNSEEN;
}

// Makes the temporary file beside the target, `<dir>/.<name>.<pid>.shale-tmp`,
// mode 0600 whatever the umask: 0 with `s->fd` open, the errno, or
// SWAP_NO_ROOM when `a`, where its name is made, refused.
static int swap_file_open(SwapFile *s, Arena *a) {
    Str path = {s->path.bytes, strlen(s->path.bytes)};
    Str dir = path;
    while (dir.len > 0 && str_byte(dir, dir.len - 1) != '/') dir.len--;
    Str base = str_sub(path, dir.len, path.len);
    if (dir.len > 1) dir.len--;
    if (dir.len == 0) dir = S("/");
    Buf t = buf_make(a);
    if (buf_str(&t, dir) != OK || buf_lit(&t, "/.") != OK || buf_str(&t, base) != OK || buf_byte(&t, '.') != OK || buf_u64(&t, (uint64_t)getpid()) != OK || buf_lit(&t, ".shale-tmp") != OK) return SWAP_NO_ROOM;
    const char *c = NULL; // cstr: NUL-ended for the kernel
    FileSystemErr e = file_system_cstr(&s->tmp, os_str_own(buf_view(&t)), &c);
    if (e != FILE_SYSTEM_OK) return ENAMETOOLONG;
    int fd = open(s->tmp.bytes, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return errno;
    // 0600 whatever the umask. Its answer is not read: the open's own mode
    // already holds the file to 0600 or less.
    (void)fchmod(fd, 0600); // ignored: the open's own mode already holds it
    s->fd = fd;
    s->made = true;
    return 0;
}

// Writes all of `bytes` to the temporary file: 0, or the errno. A write that
// takes nothing is an error, never a loop.
static inline int swap_file_write(SwapFile *s, Str bytes) {
    size_t done = 0;
    while (done < bytes.len) {
        ssize_t n = write(s->fd, &at(bytes, done), bytes.len - done);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return errno;
        if (n == 0) return EIO;
        done += (size_t)n;
    }
    return 0;
}

// Syncs the temporary file, closes it and renames it over the target; with
// `replace` false a file made at the target meanwhile is kept, SWAP_TAKEN.
// On anything but SWAP_DONE the temporary file is still there for
// swap_file_discard.
static SwapEnd swap_file_commit(SwapFile *s, bool replace, int *err) {
    *err = 0;
    if (fsync(s->fd) != 0) return SWAP_UNSYNCED;
    close(s->fd);
    s->fd = -1;
    long rc = syscall(SYS_renameat2, AT_FDCWD, s->tmp.bytes, AT_FDCWD, s->path.bytes, replace ? 0u : 1u);
    if (rc != 0) {
        *err = errno;
        return *err == EEXIST ? SWAP_TAKEN : SWAP_UNRENAMED;
    }
    s->made = false;
    return SWAP_DONE;
}

// Closes the temporary file and removes it, when it is there.
static void swap_file_discard(SwapFile *s) {
    if (s->fd >= 0) close(s->fd);
    s->fd = -1;
    if (s->made) (void)unlink(s->tmp.bytes); // ignored: a discard on a path that failed
    s->made = false;
}
