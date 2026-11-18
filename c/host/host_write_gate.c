// host_write_gate.c: where the host itself writes for the script (`run`'s
// log, a job's log and record): the path through `may_change`, so a
// read-only mount refuses it and `--undo-on-stop` journals it
// (`host_change_os_path`); and the one open of a file the host writes, or of
// a secret's file it reads: a regular file, never a link
// (`host_open_regular`).
//
// Include host.c and builtins_change.c first.

// A path the host itself writes for the script (`run`'s log, a job's log and
// record, `suffix` added to the script's path first), as an OS path, once
// `may_change` let it change.
// DRAGON: the file system's boundaries and the undo journal. A file the host
// opens for writing outside `change_may` would write through a read-only mount
// and past `--undo-on-stop`'s journal.
// REMEDY: every such open takes its path from here; test_run_guards plants a
// log into a read-only mount and one put back by an undo.
static STATUS_MUST_USE Status host_change_os_path(Host *h, Run *r, Pos pos, Str given, Str suffix, Str *out) {
    RunFiles *rs = builtin_files_of(r);
    Str full = given;
    if (suffix.len > 0) {
        Buf b = buf_make(r->ctx.values);
        if (buf_str(&b, given) != OK || buf_str(&b, suffix) != OK) return run_values_oom(r, pos);
        full = buf_view(&b);
    }
    CheckedPath p;
    TRY(path_arg(&rs->path, pos, builtin_who(r, BUILTIN_HOST), full, false, &p));
    TRY(change_may(r, pos, &p));
    return paths_absolute(&h->paths, &rs->path, pos, mounts_trim_slash(p.full.s), out);
}

// What `host_open_regular` gives besides an fd: the path is not a regular
// file (a FIFO, a device, a folder).
enum { HOST_NOT_REGULAR = -1 };

// Opens `c`, which must be a regular file, for writing (made if missing,
// truncated) or reading: 0 with `*fd` set, HOST_NOT_REGULAR, or the errno.
// DRAGON: the run's deadline and the file system's boundaries. A FIFO or a
// device at the path would hold the open, and `shale` with it, until a reader
// or a writer came; a link planted at the last part would be followed.
// REMEDY: O_NONBLOCK so the open returns at once (a FIFO with no reader is
// ENXIO), O_NOFOLLOW so a link is refused (ELOOP), then fstat must say a
// regular file before O_NONBLOCK is cleared and the fd handed back.
static int host_open_regular(const char *c, bool write, int *fd) { // cstr: NUL-ended for the kernel
    int flags = write ? O_WRONLY | O_CREAT | O_TRUNC : O_RDONLY;
    int got = open(c, flags | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0666);
    if (got < 0) return errno == ENXIO ? HOST_NOT_REGULAR : errno;
    struct stat st;
    if (fstat(got, &st) != 0) {
        int e = errno;
        close(got);
        return e;
    }
    if (!S_ISREG(st.st_mode)) {
        close(got);
        return HOST_NOT_REGULAR;
    }
    int fl = fcntl(got, F_GETFL, 0);
    if (fl >= 0) fcntl(got, F_SETFL, fl & ~O_NONBLOCK);
    *fd = got;
    return 0;
}
