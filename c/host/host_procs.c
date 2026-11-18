// host_procs.c: the host's process calls, `procs` and `proc_stop`, and the
// /proc and pidfd calls they make. A script finds a process it means by what
// `procs` gives (its `args`, its `name`) and stops that one, never one a
// pattern also matches.
//
// **Never the caller, by construction.** `pkill -f pattern` matches its own
// shell when the shell's command line holds the pattern, and kills the
// command that ran it. `procs` leaves out this process and every one above
// it, so no filter of its list can name them, and `proc_stop` refuses them
// whatever it is handed. A process is named by its pid and its start time
// together, and signalled through a pidfd opened before its start time is
// checked, so a pid the kernel has given to another since is never
// signalled. A signal refused is a stop, never a success.
//
// Include host.c first.

#include <dirent.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

// The most entries `procs` gives; past it the list ends with `{more = true}`.
enum { PROCS_MAX = 4096 };

// The most of a command line `procs` reads; past it the entry says
// `args_cut = true`, so a filter knows it saw only the start.
enum { PROCS_ARGS_MAX = 1 << 20 };

// The most processes `procs_lineage` walks up.
enum { PROCS_LINEAGE_MAX = 64 };

VEC_DEFINE(ProcsPids, int32_t)

// Room for a stat line's one read.
typedef struct {
    char bytes[1024];
} ProcsStatRoom;

// Room for a status file's one read.
typedef struct {
    char bytes[4096];
} ProcsStatusRoom;

// One process as /proc/<pid>/stat has it; `start` and `name` point into its
// ProcsStatRoom.
typedef struct {
    int32_t ppid;
    // The start time in the kernel's ticks, raw: field 22.
    Str start;
    // What the stat line holds between its first `(` and its last `)`.
    Str name;
} ProcsStat;

// This process and those above it, to pid 1's child: the ones no script may
// stop.
typedef struct {
    int32_t pids[PROCS_LINEAGE_MAX];
    size_t len;
} ProcsLineage;

// `/proc/<pid>/<what>` read once into `room`, as Zig's `readProc`: false when
// it cannot be opened or read.
static bool procs_read(int32_t pid, const char *what, Bytes room, Str *out) { // cstr: a literal file name
    char path[64];
    int n = snprintf(path, sizeof path, "/proc/%d/%s", (int)pid, what);
    if (n <= 0 || (size_t)n >= sizeof path) return false;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t got = read(fd, room.ptr, room.len);
    close(fd);
    if (got < 0) return false;
    *out = (Str){room.ptr, (size_t)got};
    return true;
}

// A decimal integer the whole of `s`, as Zig's `parseInt` reads the kernel's
// numbers: an optional sign, then digits. False for anything else or past
// `int64_t`.
static bool procs_parse_int(Str s, int64_t *out) {
    size_t i = 0;
    bool negative = false;
    if (s.len > 0 && (str_byte(s, 0) == '-' || str_byte(s, 0) == '+')) {
        negative = str_byte(s, 0) == '-';
        i = 1;
    }
    if (i == s.len) return false;
    uint64_t v = 0;
    for (; i < s.len; i++) {
        uint8_t c = str_byte(s, i);
        if (c < '0' || c > '9') return false;
        uint64_t d = (uint64_t)(c - '0');
        if (v > (UINT64_MAX - d) / 10) return false;
        v = v * 10 + d;
    }
    if (v > (uint64_t)INT64_MAX + (negative ? 1 : 0)) return false;
    *out = negative ? (int64_t)(0 - v) : (int64_t)v;
    return true;
}

// The process `pid` as its stat line has it, read into `room`; false for one
// not there. The fields are counted after the last `)`, since a program's
// name may hold one.
static bool procs_stat(ProcsStatRoom *room, int32_t pid, ProcsStat *out) {
    Bytes b = {room->bytes, sizeof room->bytes};
    Str text = S("");
    if (!procs_read(pid, "stat", b, &text)) return false;
    size_t open_at = SIZE_MAX;
    for (size_t i = 0; i < text.len; i++) {
        if (str_byte(text, i) == '(') {
            open_at = i;
            break;
        }
    }
    if (open_at == SIZE_MAX) return false;
    size_t close_at = SIZE_MAX;
    for (size_t i = text.len; i > 0; i--) {
        if (str_byte(text, i - 1) == ')') {
            close_at = i - 1;
            break;
        }
    }
    if (close_at == SIZE_MAX || close_at < open_at) return false;
    Str rest = str_sub(text, close_at + 1, text.len);
    int64_t ppid = 0;
    // Field 3 (the state) is the first after `)`; the parent is 4, the start
    // 22. The fields are read in order, so the parent is read (or refused)
    // before the start is reached.
    size_t field = 3;
    size_t i = 0;
    while (i < rest.len) {
        while (i < rest.len && str_byte(rest, i) == ' ') i++;
        if (i == rest.len) break;
        size_t from = i;
        while (i < rest.len && str_byte(rest, i) != ' ') i++;
        Str f = str_sub(rest, from, i);
        if (field == 4) {
            if (!procs_parse_int(f, &ppid) || ppid < INT32_MIN || ppid > INT32_MAX) return false;
        }
        if (field == 22) {
            out->ppid = (int32_t)ppid;
            out->start = f;
            out->name = str_sub(text, open_at + 1, close_at);
            return true;
        }
        field++;
    }
    return false;
}

// The real uid in /proc/<pid>/status, or false.
static bool procs_uid(int32_t pid, int64_t *out) {
    ProcsStatusRoom room;
    Bytes b = {room.bytes, sizeof room.bytes};
    Str text = S("");
    if (!procs_read(pid, "status", b, &text)) return false;
    size_t i = 0;
    while (i <= text.len) {
        size_t end = i;
        while (end < text.len && str_byte(text, end) != '\n') end++;
        Str line = str_sub(text, i, end);
        if (str_starts_with(line, S("Uid:"))) {
            size_t k = 4;
            while (k < line.len && (str_byte(line, k) == ' ' || str_byte(line, k) == '\t')) k++;
            size_t from = k;
            while (k < line.len && str_byte(line, k) != ' ' && str_byte(line, k) != '\t') k++;
            if (k == from) return false;
            return procs_parse_int(str_sub(line, from, k), out);
        }
        i = end + 1;
    }
    return false;
}

// The whole of /proc/<pid>/cmdline up to PROCS_ARGS_MAX bytes, its NULs
// spaces, into the values' arena; `*gone` for a process gone (it cannot be
// opened or read), `*cut` when there was more.
static STATUS_MUST_USE Status procs_cmdline(Run *r, Pos pos, int32_t pid, Str *out, bool *gone, bool *cut) {
    *gone = false;
    *cut = false;
    char path[64];
    int n = snprintf(path, sizeof path, "/proc/%d/cmdline", (int)pid);
    if (n <= 0 || (size_t)n >= sizeof path) {
        *gone = true;
        return OK;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        *gone = true;
        return OK;
    }
    Buf b = buf_make(r->ctx.values);
    char chunk[4096];
    bool refused = false;
    for (;;) {
        ssize_t got = read(fd, chunk, sizeof chunk);
        if (got < 0) {
            *gone = true;
            break;
        }
        if (got == 0) break;
        Str piece = {chunk, (size_t)got};
        bool over = b.len + piece.len > PROCS_ARGS_MAX;
        if (over) piece = str_sub(piece, 0, PROCS_ARGS_MAX - b.len);
        if (buf_str(&b, piece) != OK) {
            refused = true;
            break;
        }
        if (over) {
            *cut = true;
            break;
        }
    }
    close(fd);
    if (refused) return run_values_oom(r, pos);
    if (*gone) return OK;
    size_t len = b.len;
    Bytes args = {b.ptr, b.len};
    while (len > 0 && at(args, len - 1) == 0) len--;
    for (size_t i = 0; i < len; i++) {
        if (at(args, i) == 0) at(args, i) = ' ';
    }
    *out = (Str){b.ptr, len};
    return OK;
}

// This process and every one above it, to pid 1's child.
static void procs_lineage(ProcsLineage *l) {
    l->len = 0;
    SLICE_TYPE(ProcsLineagePids, int32_t);
    ProcsLineagePids room = {l->pids, PROCS_LINEAGE_MAX};
    int32_t pid = (int32_t)getpid();
    ProcsStatRoom buf;
    while (pid > 1 && l->len < room.len) {
        at(room, l->len) = pid;
        l->len++;
        ProcsStat s;
        if (!procs_stat(&buf, pid, &s)) break;
        pid = s.ppid;
    }
}

static bool procs_in_lineage(const ProcsLineage *l, int32_t pid) {
    SLICE_TYPE(ProcsLineageView, const int32_t);
    ProcsLineageView v = {l->pids, l->len};
    for (size_t i = 0; i < v.len; i++) {
        if (at(v, i) == pid) return true;
    }
    return false;
}

static int procs_pid_order(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a;
    int32_t y = *(const int32_t *)b;
    return (x > y) - (x < y);
}

// Every pid /proc lists, into `out`. `*why` is "" when listed, else what the
// stop says: /proc cannot be read (opened), or cannot be listed (its listing
// failed part way).
static STATUS_MUST_USE Status procs_pids(Run *r, Pos pos, ProcsPids *out, Str *why) {
    *why = S("");
    DIR *d = opendir("/proc");
    if (d == NULL) {
        *why = S("`procs`: /proc cannot be read");
        return OK;
    }
    for (;;) {
        errno = 0;
        struct dirent *ent = readdir(d);
        if (ent == NULL) {
            if (errno != 0) *why = S("`procs`: /proc cannot be listed");
            break;
        }
        int64_t pid = 0;
        Str name = {ent->d_name, strlen(ent->d_name)};
        if (!procs_parse_int(name, &pid) || pid < INT32_MIN || pid > INT32_MAX) continue;
        if (ProcsPids_push(out, (int32_t)pid) != OK) {
            closedir(d);
            return run_values_oom(r, pos);
        }
    }
    closedir(d);
    return OK;
}

// One entry of `procs()`: `{pid, ppid, uid, name, args[, args_cut], start}`.
static STATUS_MUST_USE Status procs_entry(Run *r, Pos pos, int32_t pid, const ProcsStat *s, Str args, bool cut, Value *out) {
    Table *m = NULL;
    TRY(run_new_table(r, pos, &m));
    m->kind = TABLE_MAP;
    TRY(run_map_put_int(r, pos, m, S("pid"), pid));
    TRY(run_map_put_int(r, pos, m, S("ppid"), s->ppid));
    Value uid;
    int64_t u = 0;
    if (procs_uid(pid, &u)) {
        value_int(&uid, u);
    } else {
        value_nil(&uid);
    }
    TRY(run_map_put(r, pos, m, S("uid"), &uid));
    Str name = S("");
    if (!arena_dup(r->ctx.values, s->name, &name)) return run_values_oom(r, pos);
    TRY(run_map_put_str(r, pos, m, S("name"), name));
    TRY(run_map_put_str(r, pos, m, S("args"), args));
    if (cut) TRY(run_map_put_bool(r, pos, m, S("args_cut"), true));
    Str start = S("");
    if (!arena_dup(r->ctx.values, s->start, &start)) return run_values_oom(r, pos);
    TRY(run_map_put_str(r, pos, m, S("start"), start));
    value_table(out, m);
    return OK;
}

// `procs()`: every process but this one and those above it, in pid order.
// `args` is its command line joined by spaces (with `args_cut = true` past
// PROCS_ARGS_MAX), `start` its start time in the kernel's ticks (what
// `proc_stop` checks it by).
static STATUS_MUST_USE Status procs_list(Run *r, Pos pos, Value *out) {
    ProcsLineage mine;
    procs_lineage(&mine);
    ProcsPids listed = ProcsPids_make(r->ctx.values);
    Str why = S("");
    TRY(procs_pids(r, pos, &listed, &why));
    if (why.len > 0) return host_fail_plain(r, pos, CLASS_HOST, why);
    ProcsPids pids = ProcsPids_make(r->ctx.values);
    for (size_t i = 0; i < listed.len; i++) {
        int32_t pid = at(listed, i);
        if (procs_in_lineage(&mine, pid)) continue;
        if (ProcsPids_push(&pids, pid) != OK) return run_values_oom(r, pos);
    }
    if (pids.len > 1) qsort(pids.ptr, pids.len, sizeof *pids.ptr, procs_pid_order);
    Table *list = NULL;
    TRY(run_new_table(r, pos, &list));
    list->kind = TABLE_LIST;
    ProcsStatRoom buf;
    for (size_t i = 0; i < pids.len; i++) {
        int32_t pid = at(pids, i);
        if (list->list.len == PROCS_MAX) {
            Table *more = NULL;
            TRY(run_new_table(r, pos, &more));
            more->kind = TABLE_MAP;
            TRY(run_map_put_bool(r, pos, more, S("more"), true));
            Value mv;
            value_table(&mv, more);
            TRY(run_list_push(r, pos, list, &mv));
            break;
        }
        // A process gone between the listing and its read is passed by.
        ProcsStat s;
        if (!procs_stat(&buf, pid, &s)) continue;
        Str args = S("");
        bool gone = false;
        bool cut = false;
        TRY(procs_cmdline(r, pos, pid, &args, &gone, &cut));
        if (gone) continue;
        TRY(run_charge(r, pos, 1 + args.len / 16));
        Value e;
        TRY(procs_entry(r, pos, pid, &s, args, cut, &e));
        TRY(run_list_push(r, pos, list, &e));
    }
    value_table(out, list);
    return OK;
}

// Zig's `std.os.linux.E` tag of the errors a pidfd's open or signal gives,
// as `{t}` names them; the rest as `stdout_secret`'s names them.
static Str procs_errno_tag(int e) {
    switch (e) {
    case ESRCH: return S("SRCH");
    case ENOMEM: return S("NOMEM");
    case ENOSYS: return S("NOSYS");
    case ENODEV: return S("NODEV");
    case EAGAIN: return S("AGAIN");
    default: return host_errno_tag(e);
    }
}

// Whether `pid` is still the process that started at `start`.
static bool procs_same(int32_t pid, Str start) {
    ProcsStatRoom buf;
    ProcsStat s;
    if (!procs_stat(&buf, pid, &s)) return false;
    return str_eq(s.start, start);
}

// Whether the process `fd` names ends within `ms`: a pidfd turns readable
// when its process exits, a zombie too. Zig's `pidfdEnds`, one poll whatever
// interrupts it.
static bool procs_pidfd_ends(int fd, int ms) {
    struct pollfd p = {fd, POLLIN, 0};
    return poll(&p, 1, ms) == 1;
}

// `sig` (named `sig_name`) to the process `fd` names: `*sent` false when it
// has gone already; any refusal (EPERM, for one the run may not signal)
// stops the run.
static STATUS_MUST_USE Status procs_signal(Run *r, Pos pos, int fd, int32_t pid, int sig, Str sig_name, bool *sent) {
    *sent = false;
    if (syscall(SYS_pidfd_send_signal, fd, sig, NULL, 0) == 0) {
        *sent = true;
        return OK;
    }
    int e = errno;
    if (e == ESRCH) return OK;
    return host_fail(r, pos, CLASS_HOST, S("`proc_stop`: pid {d} refused {t}: {t}"), MSG_ARGS(MSG_FROM_HOST(run_num(r, pid)), MSG_FROM_HOST(sig_name), MSG_FROM_HOST(procs_errno_tag(e))));
}

// `proc_stop`'s entry checked: its pid and its start. Zig's `stop` before
// its first charge.
static STATUS_MUST_USE Status procs_stop_target(Run *r, Pos pos, const Value *v, int32_t *pid, Str *start) {
    if (v->tag != VALUE_TABLE) return host_fail(r, pos, CLASS_TYPE, S("`proc_stop` takes an entry of `procs()`, not {s}"), MSG_ARGS(MSG_FROM_HOST(value_type_name(v))));
    const Table *m = v->as.table;
    if (m->kind != TABLE_MAP) return host_fail_plain(r, pos, CLASS_TYPE, S("`proc_stop` takes an entry of `procs()`, a map, not a list"));
    Value p;
    if (!ValueMap_get(&m->map, S("pid"), &p)) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`proc_stop`: the entry has no `pid`: hand it one of `procs()`"));
    if (p.tag != VALUE_INT) return host_fail_plain(r, pos, CLASS_TYPE, S("`proc_stop`: the entry's `pid` is not an integer"));
    if (p.as.integer < INT32_MIN || p.as.integer > INT32_MAX) return host_fail(r, pos, CLASS_ARGUMENT, S("`proc_stop`: pid {d} is no process's"), MSG_ARGS(MSG_FROM_HOST(run_num(r, p.as.integer))));
    *pid = (int32_t)p.as.integer;
    Value s;
    if (!ValueMap_get(&m->map, S("start"), &s)) return host_fail_plain(r, pos, CLASS_ARGUMENT, S("`proc_stop`: the entry has no `start`: hand it one of `procs()`, which names a process by its pid and its start"));
    if (s.tag != VALUE_STRING) return host_fail_plain(r, pos, CLASS_TYPE, S("`proc_stop`: the entry's `start` is not a string"));
    *start = s.as.string;
    // DRAGON: the process boundary. A TERM to this `shale` or to a process
    // above it (the shell, the agent's tool call) kills the command that ran
    // the script, as `pkill -f` matching its own shell does.
    // REMEDY: refused here for any entry handed, whatever its start, before
    // a pidfd is opened; `procs` leaves the same lineage out, so no filter
    // of its list names one. test_host_procs plants a lineage that starts
    // above this process.
    ProcsLineage l;
    procs_lineage(&l);
    if (*pid <= 1 || procs_in_lineage(&l, *pid)) return host_fail(r, pos, CLASS_ARGUMENT, S("`proc_stop`: pid {d} is this run or one above it, which a script never stops"), MSG_ARGS(MSG_FROM_HOST(run_num(r, *pid))));
    return OK;
}

// The stop of `pid` through `fd`, its pidfd, once it is open: `*out` true
// when it was `p` and has ended.
static STATUS_MUST_USE Status procs_stop_through(Host *h, Run *r, Pos pos, int fd, int32_t pid, Str start, Value *out) {
    if (!procs_same(pid, start)) return OK;
    bool sent = false;
    TRY(procs_signal(r, pos, fd, pid, SIGTERM, S("TERM"), &sent));
    if (!sent) return OK;
    if (!procs_pidfd_ends(fd, 2000)) {
        TRY(procs_signal(r, pos, fd, pid, SIGKILL, S("KILL"), &sent));
        if (!procs_pidfd_ends(fd, 2000)) return host_fail(r, pos, CLASS_HOST, S("`proc_stop`: pid {d} is still there 2 seconds after KILL"), MSG_ARGS(MSG_FROM_HOST(run_num(r, pid))));
    }
    Buf b = buf_make(r->ctx.values);
    if (buf_lit(&b, "process ") == OK && buf_u64(&b, (uint64_t)pid) == OK && buf_lit(&b, " stopped") == OK) host_record(h, buf_view(&b));
    value_bool(out, true);
    return OK;
}

// `proc_stop(p)`: TERM to the process `p` (an entry of `procs()`), 2
// seconds, then KILL; whether it was running. Refused for this process and
// those above it; a process whose start time is not `p.start` is not `p`,
// and is left alone (false). **A signal refused is a stop** (the box's
// review): a process the run may not signal stops the run, named, never a
// success with the process still there; nor is one still there 2 seconds
// after KILL.
static STATUS_MUST_USE Status procs_stop(Host *h, Run *r, Pos pos, const Value *v, Value *out) {
    int32_t pid = 0;
    Str start = S("");
    TRY(procs_stop_target(r, pos, v, &pid, &start));
    TRY(run_charge(r, pos, 1));
    value_bool(out, false);
    // DRAGON: the process boundary. A pid names whichever process holds it
    // now, so a signal by pid after the start time is checked could reach a
    // process the kernel gave the pid to between the two.
    // REMEDY: the pidfd names the process that held the pid when it was
    // opened, whatever the pid comes to name; its start time, checked after,
    // says that process is `p`, and every signal goes through the pidfd.
    // test_host_procs plants a start left unchecked.
    int fd = (int)syscall(SYS_pidfd_open, pid, 0);
    if (fd < 0) {
        int e = errno;
        if (e == ESRCH) return OK;
        return host_fail(r, pos, CLASS_HOST, S("`proc_stop`: pid {d}: pidfd_open: {t}"), MSG_ARGS(MSG_FROM_HOST(run_num(r, pid)), MSG_FROM_HOST(procs_errno_tag(e))));
    }
    Status s = procs_stop_through(h, r, pos, fd, pid, start, out);
    close(fd);
    return s;
}

// `procs` and `proc_stop`; `*mine` says whether `name` is one of them.
static STATUS_MUST_USE Status host_procs_call(Host *h, Run *r, Pos pos, Str name, ConstValues args, Value *out, bool *mine) {
    *mine = true;
    if (str_eq(name, S("procs"))) {
        if (args.len != 0) return host_fail(r, pos, CLASS_ARGUMENT, S("`procs` takes nothing, given {d} arguments: filter its list by `args` or `name`"), MSG_ARGS(MSG_FROM_HOST(run_unum(r, args.len))));
        return procs_list(r, pos, out);
    }
    if (str_eq(name, S("proc_stop"))) {
        if (args.len != 1) return host_fail(r, pos, CLASS_ARGUMENT, S("`proc_stop` takes one entry of `procs()`, given {d} arguments"), MSG_ARGS(MSG_FROM_HOST(run_unum(r, args.len))));
        return procs_stop(h, r, pos, &at(args, 0), out);
    }
    *mine = false;
    return OK;
}
