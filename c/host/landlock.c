// landlock.c: Landlock, the kernel's own confinement of a process's files
// (Linux 5.13 and later, no privilege needed), in raw system calls: the ABI
// version the kernel speaks, a ruleset that handles every file right that
// version knows, and a rule that allows some of them beneath a path. A
// process that restricts itself to a ruleset (spawn_child.c, between fork and
// exec) and every process it starts after keep it for good: no later call
// widens it.
//
// glibc wraps none of these calls, so their numbers and structs are the
// kernel's uapi ones (linux/landlock.h), written out here for x86-64, the
// only machine C Shale builds for.
//
// The including file defines _GNU_SOURCE before its first include, for
// syscall and O_PATH. Its functions are `static inline`: a unit of the
// spawn alone (test_spawn, test_jobs) includes it for spawn_child.c's
// restrict and calls none of them.

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

// x86-64's numbers for the three calls.
enum {
    LANDLOCK_SYS_CREATE_RULESET = 444,
    LANDLOCK_SYS_ADD_RULE = 445,
    LANDLOCK_SYS_RESTRICT_SELF = 446,
};

// `landlock_create_ruleset`'s flag that asks for the ABI version.
enum { LANDLOCK_VERSION_FLAG = 1 };

// `landlock_add_rule`'s one rule type for files: rights beneath a path.
enum { LANDLOCK_RULE_BENEATH = 1 };

// The file rights, by the ABI version that brought each.
enum {
    LANDLOCK_EXECUTE = 1 << 0,
    LANDLOCK_WRITE_FILE = 1 << 1,
    LANDLOCK_READ_FILE = 1 << 2,
    LANDLOCK_READ_DIR = 1 << 3,
    LANDLOCK_REMOVE_DIR = 1 << 4,
    LANDLOCK_REMOVE_FILE = 1 << 5,
    LANDLOCK_MAKE_CHAR = 1 << 6,
    LANDLOCK_MAKE_DIR = 1 << 7,
    LANDLOCK_MAKE_REG = 1 << 8,
    LANDLOCK_MAKE_SOCK = 1 << 9,
    LANDLOCK_MAKE_FIFO = 1 << 10,
    LANDLOCK_MAKE_BLOCK = 1 << 11,
    LANDLOCK_MAKE_SYM = 1 << 12,
    // ABI 2: a link or a rename that moves a file to another folder.
    LANDLOCK_REFER = 1 << 13,
    // ABI 3: truncate(2), ftruncate(2) and an open's O_TRUNC.
    LANDLOCK_TRUNCATE = 1 << 14,
    // ABI 5: an ioctl on a device file.
    LANDLOCK_IOCTL_DEV = 1 << 15,
};

// The ABI version a confined program needs, Linux 6.2's.
// DRAGON: the file system's boundaries. Below ABI 2 no ruleset can allow a
// rename into another folder, so git (an object made in a temporary file,
// then moved) breaks under any; below ABI 3 truncate(2) is outside
// Landlock, so a program could empty any file of the user's by its path,
// mounts or not.
// REMEDY: a kernel below ABI 3 runs no program at all (host_confine.c's
// stop says so); none runs unconfined on it instead.
enum { LANDLOCK_ABI_NEEDED = 3 };

// The rights a rule on a file, not a folder, may hold: the kernel refuses
// a folder's rights (listing, making, removing) on a file.
enum { LANDLOCK_FILE_RIGHTS = LANDLOCK_EXECUTE | LANDLOCK_WRITE_FILE | LANDLOCK_READ_FILE | LANDLOCK_TRUNCATE | LANDLOCK_IOCTL_DEV };

// What a folder that may only be read allows: its files read and run, its
// folders listed.
enum { LANDLOCK_READ_RIGHTS = LANDLOCK_EXECUTE | LANDLOCK_READ_FILE | LANDLOCK_READ_DIR };

// A device a program may read and write (/dev/null): the file read,
// written, emptied by an O_TRUNC open, and its ioctls.
enum { LANDLOCK_DEVICE_RIGHTS = LANDLOCK_READ_FILE | LANDLOCK_WRITE_FILE | LANDLOCK_TRUNCATE | LANDLOCK_IOCTL_DEV };

// The kernel's ABI version: 1 or more, or the errno that says there is none
// as a negative number (-ENOSYS for a kernel built without it, -EOPNOTSUPP
// for one booted with it off).
static inline int landlock_abi(void) {
    long v = syscall(LANDLOCK_SYS_CREATE_RULESET, NULL, (size_t)0, (uint32_t)LANDLOCK_VERSION_FLAG);
    if (v < 0) return -errno;
    return (int)v;
}

// Every file right ABI `abi` knows: a ruleset handles them all, so a right
// no rule names is refused.
static inline uint64_t landlock_handled(int abi) {
    uint64_t all = (uint64_t)LANDLOCK_MAKE_SYM << 1;
    all -= 1;
    if (abi >= 2) all |= LANDLOCK_REFER;
    if (abi >= 3) all |= LANDLOCK_TRUNCATE;
    if (abi >= 5) all |= LANDLOCK_IOCTL_DEV;
    return all;
}

// A new ruleset handling `handled`, into `*fd` (close-on-exec, as the kernel
// makes it); 0, or the errno. Only the struct's first field is passed, the
// file rights, so the network and the scopes stay unhandled whatever the
// ABI.
static inline int landlock_ruleset(uint64_t handled, int *fd) {
    uint64_t attr = handled;
    long got = syscall(LANDLOCK_SYS_CREATE_RULESET, &attr, sizeof attr, (uint32_t)0);
    if (got < 0) return errno;
    *fd = (int)got;
    return 0;
}

// The kernel's `struct landlock_path_beneath_attr`: packed, a u64 then an
// s32, 12 bytes.
typedef struct __attribute__((packed)) {
    uint64_t allowed_access;
    int32_t parent_fd;
} LandlockBeneath;

// Allows `rights` (of those `handled`) beneath `path` in the ruleset `fd`,
// every link in the path followed, so a rule holds the folder it reaches.
// A path that is a file keeps only the rights a file takes. 0, or the
// errno: ENOENT and ENOTDIR for a path that is not there.
static inline int landlock_allow(int fd, const char *path, uint64_t rights, uint64_t handled) { // cstr: NUL-ended for the kernel
    int at_fd = open(path, O_PATH | O_CLOEXEC);
    if (at_fd < 0) return errno;
    struct stat st;
    if (fstat(at_fd, &st) != 0) {
        int e = errno;
        close(at_fd);
        return e;
    }
    uint64_t allowed = rights & handled;
    if (!S_ISDIR(st.st_mode)) allowed &= LANDLOCK_FILE_RIGHTS;
    LandlockBeneath attr = {allowed, at_fd};
    long got = syscall(LANDLOCK_SYS_ADD_RULE, fd, LANDLOCK_RULE_BENEATH, &attr, (uint32_t)0);
    int e = got < 0 ? errno : 0;
    close(at_fd);
    return e;
}
