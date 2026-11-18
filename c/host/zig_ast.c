// zig_ast.c: the run's zig-ast and its answers, what every Zig call shares:
// the one `zig-ast --serve` process a run starts at its first Zig call
// (`host_zig_proc_of`), a request sent and its answer read in order
// (`host_zig_ask`), a stop for a zig-ast gone wrong (`host_zig_ended`), and
// an answer's edits made into a file's new text (`host_zig_apply`) and
// written as `edit`'s are (`host_zig_rewrite`). The calls themselves are
// host_zig.c.
//
// It is `tools:zig-ast/bin/zig-ast`, or the program `SHALE_ZIG_AST` names,
// and it ends with the run (`host_zig_stop`), so a recipe that reads a
// hundred files pays one program. It is the run's, a HostZigProc its owner
// holds (c/shale.c's run, the tests' Ran) and hands the host (`Host.zig`),
// never a static.
//
// Include spawn_argv.c, pgroup.c, spawn_child.c, zig_wire.c, paths.c and
// builtins_change.c first.

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// The bytes of zig-ast's answers read ahead: Zig's reader's buffer, which
// also bounds a line `take_line` reads.
enum { HOST_ZIG_BUF = 64 * 1024 };

// A NUL-ended string of the system's, as a Str.
static Str host_zig_from_c(const char *s) { // cstr: the system's own
    Str out = {s, strlen(s)};
    return out;
}

// How long `host_zig_stop` waits for zig-ast to exit once its stdin is shut,
// and how long a stop's message waits for what it said on stderr.
enum { HOST_ZIG_EXIT_WAIT_MS = 2000, HOST_ZIG_STDERR_WAIT_MS = 2000 };

// How long one request may wait on zig-ast (its header, a write, an
// answer's bytes) before the call stops, unless the run sets another
// (`HostZigProc.answer_ms`, which a test sets shorter).
enum { HOST_ZIG_ANSWER_MS = 60000 };

// The run's zig-ast: its pid, its three pipes, and the answers read ahead.
// DRAGON: the run's deadline. zig-ast is a program of its own, and a hung
// one, or one that fills its stderr pipe and blocks there, would hold the
// run forever: a plain read waits without bound.
// REMEDY: every wait polls under the request's `deadline` and drains stderr
// into `said` the while (`host_zig_wait`); past it zig-ast is killed and the
// call stops, saying so. test_zig_wire plants a hung one and a flood.
typedef struct HostZigProc {
    bool started;
    pid_t pid;
    int in_w;
    int out_r;
    int err_r;
    char room[HOST_ZIG_BUF];
    size_t from;
    size_t to;
    // The first bytes zig-ast wrote on stderr while serving the request,
    // for a stop's message; the rest is read and dropped.
    char said[1024];
    size_t said_len;
    // When the request being served must be done, on the monotonic clock.
    uint64_t deadline;
    // Set when a wait passed the deadline and zig-ast was killed.
    bool late;
    // How long one request may wait on zig-ast.
    uint64_t answer_ms;
} HostZigProc;

// Makes `*p` a run's zig-ast not yet started, which the run's first Zig call
// starts and `host_zig_stop` ends.
static inline void host_zig_init(HostZigProc *p) {
    memset(p, 0, sizeof *p);
    p->in_w = -1;
    p->out_r = -1;
    p->err_r = -1;
    p->answer_ms = HOST_ZIG_ANSWER_MS;
}

static void host_zig_close(int *fd) {
    if (*fd >= 0) close(*fd);
    *fd = -1;
}

static void host_zig_quiet(int sig) {
    (void)sig;
}

// Ends the run's zig-ast, if one started: its stdin shut, so it exits, and
// it is reaped.
// DRAGON: the process boundary. A zig-ast left running would outlive the run
// and hold its pipes.
// REMEDY: the CLI calls this as soon as the VM returns, stopped or not,
// before any report line, so none of its early returns skips it; a zig-ast
// that does not exit on its stdin's end within HOST_ZIG_EXIT_WAIT_MS gets
// KILL, its group with it, before the reap; and its death signal ends it
// should `shale` itself be killed first.
static void host_zig_stop(HostZigProc *p) {
    if (!p->started) return;
    host_zig_close(&p->in_w);
    int status = 0;
    struct rusage usage;
    bool reaped = false;
    for (int waited = 0; waited < HOST_ZIG_EXIT_WAIT_MS; waited += 10) {
        reaped = spawn_reaped_now(p->pid, &status, &usage);
        if (reaped) break;
        struct timespec ten = {0, 10 * 1000 * 1000};
        nanosleep(&ten, NULL);
    }
    if (!reaped) {
        pgroup_signal(p->pid, SIGKILL);
        spawn_reap(p->pid, &status, NULL);
    }
    host_zig_close(&p->out_r);
    host_zig_close(&p->err_r);
    p->started = false;
}

// The paths an exec of `exe` tries: itself when it has a `/`, else in each
// folder of PATH, as Zig's spawn searches. False when the arena refuses.
static bool host_zig_candidates(Arena *a, Str exe, SpawnCStrs *out) {
    const char *path_c = getenv("PATH"); // cstr: the environment's
    Str path = path_c != NULL ? host_zig_from_c(path_c) : S("/usr/local/bin:/bin/:/usr/bin");
    if (str_find_byte(exe, '/') != NULL) path = S("");
    size_t n = 1;
    for (size_t i = 0; i < path.len; i++) n += at(path, i) == ':';
    char **list = ARENA_ARRAY(a, char *, n);
    if (list == NULL) return false;
    SpawnCStrs all = {list, n};
    size_t from = 0;
    size_t k = 0;
    for (size_t i = 0; i <= path.len; i++) {
        if (i < path.len && at(path, i) != ':') continue;
        Str dir = str_sub(path, from, i);
        from = i + 1;
        Buf b = buf_make(a);
        if (dir.len > 0 && (buf_str(&b, dir) != OK || buf_byte(&b, '/') != OK)) return false;
        if (buf_str(&b, exe) != OK) return false;
        at(all, k) = arena_cstr(a, buf_view(&b));
        if (at(all, k) == NULL) return false;
        k++;
    }
    *out = all;
    return true;
}

static uint64_t host_zig_mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

// Takes what stderr holds now into `said`, dropping what does not fit; at
// its end the pipe is closed.
static void host_zig_drain_err(HostZigProc *p) {
    char scrap[4096];
    Bytes said = {p->said, sizeof p->said};
    bool room = p->said_len < said.len;
    ssize_t n = room ? read(p->err_r, &at(said, p->said_len), said.len - p->said_len) : read(p->err_r, scrap, sizeof scrap);
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
    if (n <= 0) {
        host_zig_close(&p->err_r);
        return;
    }
    if (room) p->said_len += (size_t)n;
}

// Waits until `fd` is ready for `events`, draining stderr the while; false
// once the deadline passes, zig-ast killed then (Zig's `Proc.wait`).
static bool host_zig_wait(HostZigProc *p, int fd, short events) {
    for (;;) {
        uint64_t now = host_zig_mono_ms();
        if (now >= p->deadline) {
            pgroup_signal(p->pid, SIGKILL);
            p->late = true;
            return false;
        }
        uint64_t left = p->deadline - now;
        SpawnPollFd fds_room[2];
        SpawnPollFds fds = {fds_room, 2};
        at(fds, 0) = (SpawnPollFd){fd, events, 0};
        at(fds, 1) = (SpawnPollFd){p->err_r, POLLIN, 0};
        if (poll(fds.ptr, 2, left > INT32_MAX ? INT32_MAX : (int)left) < 0) {
            // Two open descriptors of the run's own: a poll that fails but
            // for a signal is a bug in the C, and would spin to the deadline.
            if (errno == EINTR) continue;
            c_bug("zig_ast: a poll of zig-ast's own pipes failed");
        }
        if (at(fds, 1).revents != 0) host_zig_drain_err(p);
        if (at(fds, 0).revents != 0) return true;
    }
}

// Fills the read-ahead from zig-ast's stdout: false at its end, on an error,
// or past the deadline.
static bool host_zig_fill(HostZigProc *p) {
    for (;;) {
        if (!host_zig_wait(p, p->out_r, POLLIN)) return false;
        ssize_t n = read(p->out_r, p->room, sizeof p->room);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p->from = 0;
        p->to = (size_t)n;
        return true;
    }
}

// One line of zig-ast's stdout, its newline taken off, Zig's
// `takeDelimiter`: the rest at the end of the stream, and false for an end
// with nothing, an error, or a line past the read-ahead.
static bool host_zig_take_line(HostZigProc *p, Arena *a, Str *out) {
    Buf b = buf_make(a);
    bool any = false;
    for (;;) {
        if (p->from == p->to && !host_zig_fill(p)) {
            *out = buf_view(&b);
            return any;
        }
        Bytes room = {p->room, sizeof p->room};
        char c = at(room, p->from);
        p->from++;
        any = true;
        if (c == '\n') break;
        if (b.len + 1 >= HOST_ZIG_BUF) return false;
        if (buf_byte(&b, c) != OK) return false;
    }
    *out = buf_view(&b);
    return true;
}

// `n` bytes of zig-ast's stdout, Zig's `readAllocAll`: false when it ends
// first or the scratch refuses them.
static bool host_zig_take(HostZigProc *p, Arena *a, size_t n, Str *out) {
    char *dst = arena_alloc(a, n, 1);
    if (dst == NULL && n > 0) return false;
    Bytes into = {dst, n};
    size_t got = 0;
    while (got < n) {
        if (p->from == p->to && !host_zig_fill(p)) return false;
        size_t have = p->to - p->from;
        size_t take = have < n - got ? have : n - got;
        Bytes from = {p->room, sizeof p->room};
        mem_copy(&at(into, got), &at(from, p->from), take);
        got += take;
        p->from += take;
    }
    out->ptr = dst;
    out->len = n;
    return true;
}

// Writes `s` to zig-ast's stdin (non-blocking since its start): false when
// it has gone or the deadline passes.
static bool host_zig_write_all(HostZigProc *p, Str s) {
    size_t done = 0;
    while (done < s.len) {
        if (!host_zig_wait(p, p->in_w, POLLOUT)) return false;
        ssize_t w = write(p->in_w, &at(s, done), s.len - done);
        if (w < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (w <= 0) return false;
        done += (size_t)w;
    }
    return true;
}

// Stops the run for a zig-ast that went wrong, with what it said on stderr:
// what the waits drained, or else one read within
// HOST_ZIG_STDERR_WAIT_MS, its spaces and newlines trimmed. One past the
// deadline says so in place of `why`.
static STATUS_MUST_USE Status host_zig_ended(HostZigProc *p, Run *r, Pos pos, Str who, Str why) {
    struct pollfd fd = {p->err_r, POLLIN, 0};
    if (p->said_len == 0 && p->err_r >= 0 && poll(&fd, 1, HOST_ZIG_STDERR_WAIT_MS) > 0) host_zig_drain_err(p);
    Str got = {p->said, p->said_len};
    size_t a = 0;
    size_t b = got.len;
    while (a < b && (at(got, a) == ' ' || at(got, a) == '\n')) a++;
    while (b > a && (at(got, b - 1) == ' ' || at(got, b - 1) == '\n')) b--;
    Str said = str_sub(got, a, b);
    if (p->late) {
        Buf late = buf_make(r->ctx.values);
        if (buf_lit(&late, "did not answer within ") == OK && buf_u64(&late, p->answer_ms) == OK && buf_lit(&late, " ms") == OK) why = buf_view(&late);
    }
    return run_failf(r, pos, CLASS_HOST, "`{s}`: zig-ast {s}{s}{s}", S("`"), who, S("`: zig-ast "), why, said.len > 0 ? S(": ") : S(""), said);
}

// The run's zig-ast, started if it is not yet. A start
// counts against the `programs` budget, as any program does.
static STATUS_MUST_USE Status host_zig_proc_of(HostZigProc *p, Paths *h, Run *r, Pos pos, Str who) {
    if (p->started) return OK;
    Arena *a = r->ctx.values;
    bool has_exe = false;
    Str exe = S("");
    const char *pinned = getenv("SHALE_ZIG_AST"); // cstr: the environment's
    if (pinned != NULL) {
        has_exe = true;
        exe = host_zig_from_c(pinned);
    } else {
        for (size_t k = 0; k < h->mounts.len; k++) {
            const Mount *m = &at(h->mounts, k);
            if (!str_eq(m->name, S("tools"))) continue;
            Buf b = buf_make(a);
            if (buf_str(&b, m->dir) != OK || buf_lit(&b, "/zig-ast/bin/zig-ast") != OK) return run_values_oom(r, pos);
            char *c = arena_cstr(a, buf_view(&b));
            if (c == NULL) return run_values_oom(r, pos);
            if (access(c, F_OK) != 0) break;
            has_exe = true;
            exe = buf_view(&b);
            break;
        }
    }
    if (!has_exe) {
        return run_failf(r, pos, CLASS_HOST, "`{s}`: no tools:zig-ast/bin/zig-ast, the program that reads Zig: Shale's `shale tools/zig_ast/install.shale` builds it",
                         S("`"), who, S("`: no tools:zig-ast/bin/zig-ast, the program that reads Zig: Shale's `shale tools/zig_ast/install.shale` builds it"));
    }
    if (r->host_runs >= r->ctx.budgets.programs) return run_budget_stop(&r->ctx, pos, BUDGET_PROGRAMS);
    r->host_runs++;
    char *argv_room[3] = {arena_cstr(a, exe), arena_cstr(a, S("--serve")), NULL};
    SpawnCStrs argv = {argv_room, 3};
    SpawnCStrs paths = {NULL, 0};
    if (at(argv, 0) == NULL || at(argv, 1) == NULL || !host_zig_candidates(a, exe, &paths)) return run_values_oom(r, pos);
    // A write to a zig-ast that has gone must fail with EPIPE, not end
    // `shale`: a handler that does nothing, as Zig's runtime sets, which an
    // exec puts back to the default for the program.
    struct sigaction act;
    memset(&act, 0, sizeof act);
    act.sa_handler = host_zig_quiet;
    sigemptyset(&act.sa_mask);
    sigaction(SIGPIPE, &act, NULL);
    int in_p[2] = {-1, -1};
    int out_p[2] = {-1, -1};
    int err_p[2] = {-1, -1};
    int why_p[2] = {-1, -1};
    SLICE_TYPE(HostZigPipe, int);
    HostZigPipe in_s = {in_p, 2};
    HostZigPipe out_s = {out_p, 2};
    HostZigPipe err_s = {err_p, 2};
    HostZigPipe why_s = {why_p, 2};
    int e = 0;
    pid_t pid = -1;
    if (pipe2(in_p, O_CLOEXEC) != 0 || pipe2(out_p, O_CLOEXEC) != 0 || pipe2(err_p, O_CLOEXEC) != 0 || pipe2(why_p, O_CLOEXEC) != 0) {
        e = errno;
    } else {
        // A death signal, so zig-ast never outlives `shale`, and a group of
        // its own, which the stop's KILL ends.
        SpawnChild child;
        spawn_child_default(&child);
        child.parent = getpid();
        child.stdin_fd = at(in_s, 0);
        child.stdout_fd = at(out_s, 1);
        child.stderr_fd = at(err_s, 1);
        child.paths = paths;
        child.argv = argv_room;
        child.envp = environ;
        child.report_w = at(why_s, 1);
        pid = fork();
        if (pid < 0) e = errno;
        if (pid == 0) spawn_exec_child(&child);
    }
    host_zig_close(&at(in_s, 0));
    host_zig_close(&at(out_s, 1));
    host_zig_close(&at(err_s, 1));
    host_zig_close(&at(why_s, 1));
    if (pid > 0) {
        uint32_t why = 0;
        ssize_t n = read(at(why_s, 0), &why, sizeof why);
        if (n == (ssize_t)sizeof why) {
            e = (int)why;
            int status = 0;
            spawn_reap(pid, &status, NULL);
        }
    }
    host_zig_close(&at(why_s, 0));
    if (e != 0) {
        host_zig_close(&at(in_s, 1));
        host_zig_close(&at(out_s, 0));
        host_zig_close(&at(err_s, 0));
        return run_failf(r, pos, CLASS_HOST, "`{s}`: zig-ast ({s}) did not start: {t}", S("`"), who, S("`: zig-ast ("), exe, S(") did not start: "), file_system_err_name(file_system_err_of(e)));
    }
    p->started = true;
    p->pid = pid;
    p->in_w = at(in_s, 1);
    p->out_r = at(out_s, 0);
    p->err_r = at(err_s, 0);
    p->from = 0;
    p->to = 0;
    p->said_len = 0;
    p->late = false;
    p->deadline = host_zig_mono_ms() + p->answer_ms;
    // Writes that would block wait in `host_zig_wait`, under the deadline.
    int fl = fcntl(p->in_w, F_GETFL, 0);
    if (fl >= 0) fcntl(p->in_w, F_SETFL, fl | O_NONBLOCK);
    Str said = S("");
    if (!host_zig_take_line(p, a, &said) || !str_starts_with(said, S(ZIG_WIRE_HEADER))) {
        return host_zig_ended(p, r, pos, who, S("gave no `" ZIG_WIRE_HEADER "<zig version>` line first"));
    }
    return OK;
}

// An answer's lines, read in order.
typedef struct {
    ZigWireLines lines;
    Arena *sa;
    Run *r;
    Pos pos;
    Str who;
    Str room[ZIG_WIRE_FIELDS_MAX];
    // The line `host_zig_next` gave last.
    ZigWireFields l;
} HostZigAnswer;

static STATUS_MUST_USE Status host_zig_wrong(HostZigAnswer *an) {
    return run_failf(an->r, an->pos, CLASS_HOST, "`{s}`: zig-ast's answer has a line this host does not know", S("`"), an->who, S("`: zig-ast's answer has a line this host does not know"));
}

// Sends a request, `body`, and takes the answer.
static STATUS_MUST_USE Status host_zig_ask(HostZigProc *p, Paths *h, Arena *sa, Run *r, Pos pos, Str who, Str body, HostZigAnswer *an) {
    TRY(host_zig_proc_of(p, h, r, pos, who));
    p->deadline = host_zig_mono_ms() + p->answer_ms;
    // A stop says what zig-ast wrote while it served this request.
    p->said_len = 0;
    p->late = false;
    Buf head = buf_make(sa);
    if (buf_u64(&head, body.len) != OK || buf_byte(&head, '\n') != OK) return run_values_oom(r, pos);
    if (!host_zig_write_all(p, buf_view(&head)) || !host_zig_write_all(p, body)) return host_zig_ended(p, r, pos, who, S("stopped taking requests"));
    Str len = S("");
    if (!host_zig_take_line(p, sa, &len)) return host_zig_ended(p, r, pos, who, S("ended before it answered"));
    size_t n = 0;
    if (!zig_wire_int(len, &n)) return host_zig_ended(p, r, pos, who, S("answered with no length"));
    Str got = S("");
    if (!host_zig_take(p, sa, n, &got)) return host_zig_ended(p, r, pos, who, S("ended in the middle of its answer"));
    memset(an, 0, sizeof *an);
    an->lines.body = got;
    an->sa = sa;
    an->r = r;
    an->pos = pos;
    an->who = who;
    return OK;
}

// The next line's fields into `an->l`, `*got` false at the answer's end: a
// `bad` line, or one that does not read, stops.
static STATUS_MUST_USE Status host_zig_next(HostZigAnswer *an, bool *got) {
    ZigWireFields into = {an->room, ZIG_WIRE_FIELDS_MAX};
    size_t n = 0;
    ZigWireRead rd = zig_wire_next(&an->lines, an->sa, into, &n);
    *got = false;
    if (rd == ZIG_WIRE_DONE) return OK;
    if (rd == ZIG_WIRE_OUT_OF_MEMORY) return run_values_oom(an->r, an->pos);
    if (rd != ZIG_WIRE_LINE) {
        Str e = rd == ZIG_WIRE_BAD_FRAME ? S("BadFrame") : S("BadEscape");
        return run_failf(an->r, an->pos, CLASS_HOST, "`{s}`: zig-ast's answer does not read ({t})", S("`"), an->who, S("`: zig-ast's answer does not read ("), e, S(")"));
    }
    an->l = SLICE_SUB(ZigWireFields, into, 0, n);
    if (n == 0) return host_zig_wrong(an);
    if (str_eq(at(an->l, 0), S("bad"))) {
        Str why = n > 1 ? at(an->l, 1) : S("");
        return run_failf(an->r, an->pos, CLASS_HOST, "`{s}`: zig-ast refused the request: {s}", S("`"), an->who, S("`: zig-ast refused the request: "), why);
    }
    *got = true;
    return OK;
}

static bool host_zig_is(const HostZigAnswer *an, Str word) {
    return str_eq(at(an->l, 0), word);
}

static STATUS_MUST_USE Status host_zig_int(HostZigAnswer *an, size_t i, size_t *out) {
    if (i >= an->l.len || !zig_wire_int(at(an->l, i), out)) return host_zig_wrong(an);
    return OK;
}

static STATUS_MUST_USE Status host_zig_field(HostZigAnswer *an, size_t i, Str *out) {
    if (i >= an->l.len) return host_zig_wrong(an);
    *out = at(an->l, i);
    return OK;
}

// `s` in the run's values, so it outlives the call's scratch.
static STATUS_MUST_USE Status host_zig_keep(Run *r, Pos pos, Str s, Str *out) {
    if (!arena_dup(r->ctx.values, s, out)) return run_values_oom(r, pos);
    return OK;
}

// zig-ast's count of its own work, charged as the run's steps, so a call
// costs what zig-ast spent on it.
static STATUS_MUST_USE Status host_zig_work(HostZigAnswer *an) {
    size_t n = 0;
    TRY(host_zig_int(an, 1, &n));
    return run_charge(an->r, an->pos, n);
}

// ── the request ──

static STATUS_MUST_USE Status host_zig_req_ok(Run *r, Pos pos, Status s) {
    if (s != OK) return run_values_oom(r, pos);
    return OK;
}

// A line of the request: `cmd`, then `a`, `b` if given (an empty Str is
// none), escaped.
static STATUS_MUST_USE Status host_zig_req_line(Run *r, Pos pos, Buf *w, Str cmd, Str a, Str b) {
    Status s = zig_wire_field(w, cmd);
    if (s == OK && a.ptr != NULL) s = zig_wire_tab(w);
    if (s == OK && a.ptr != NULL) s = zig_wire_field(w, a);
    if (s == OK && b.ptr != NULL) s = zig_wire_tab(w);
    if (s == OK && b.ptr != NULL) s = zig_wire_field(w, b);
    if (s == OK) s = zig_wire_end(w);
    return host_zig_req_ok(r, pos, s);
}

static const Str HOST_ZIG_NONE = {NULL, 0};

// ── edits ──

// One edit of an answer: `was[o0..o1]` becomes `text`.
typedef struct {
    size_t o0;
    size_t o1;
    Str text;
} HostZigEdit;

VEC_DEFINE(HostZigEdits, HostZigEdit)

// `was` with `edits` made, into `*now` and `*spans`, in the run's values,
// Zig's `applyEdits`; an edit out of order or past `was` is a line this host
// does not know.
static STATUS_MUST_USE Status host_zig_apply(HostZigAnswer *an, Str was, HostZigEditsItems edits, Str *now, SpansItems *spans) {
    Run *r = an->r;
    Buf out = buf_make(r->ctx.values);
    Spans sp = Spans_make(r->ctx.values);
    size_t copied = 0;
    for (size_t k = 0; k < edits.len; k++) {
        HostZigEdit e = at(edits, k);
        if (e.o0 < copied || e.o1 < e.o0 || e.o1 > was.len) return host_zig_wrong(an);
        if (buf_str(&out, str_sub(was, copied, e.o0)) != OK) return run_values_oom(r, an->pos);
        size_t c0 = out.len;
        if (buf_str(&out, e.text) != OK) return run_values_oom(r, an->pos);
        Span s = {e.o0, e.o1, c0, out.len};
        if (Spans_push(&sp, s) != OK) return run_values_oom(r, an->pos);
        copied = e.o1;
    }
    if (buf_str(&out, str_sub(was, copied, was.len)) != OK) return run_values_oom(r, an->pos);
    *now = buf_view(&out);
    *spans = Spans_items(&sp);
    return OK;
}

// An `s` line's edit, its text kept in the scratch.
static STATUS_MUST_USE Status host_zig_edit_of(HostZigAnswer *an, HostZigEdit *out) {
    TRY(host_zig_int(an, 1, &out->o0));
    TRY(host_zig_int(an, 2, &out->o1));
    return host_zig_field(an, 3, &out->text);
}

// A file a host rewrote, Zig's `rewrite`: the diff, then the change, logged
// as `edit`'s.
static STATUS_MUST_USE Status host_zig_rewrite(Run *r, Pos pos, const StorePath *p, Str was, Str now, SpansItems spans) {
    TRY(change_write_diff(r, pos, p->rel, was, now, spans));
    return change_at(r, pos, BUILTIN_EDIT, CHANGE_REPLACE, p, now);
}

// `p` read whole into the run's values, the read counted, the bytes and
// their copy charged, as Zig's `readSource` and `readKept` do.
static STATUS_MUST_USE Status host_zig_read(Run *r, Pos pos, const StorePath *p, Str *out) {
    TRY(builtin_count_read(r, pos));
    uint64_t size = 0;
    TRY(builtin_file_size(r, pos, BUILTIN_HOST, p, &size));
    TRY(builtin_read_sized(r, pos, BUILTIN_HOST, p, size, 0, BUILTIN_TO_END, out));
    return builtin_charge_bytes(r, pos, out->len);
}

// The list of a result, its kind set.
static STATUS_MUST_USE Status host_zig_list(Run *r, Pos pos, Table **out) {
    TRY(run_new_table(r, pos, out));
    (*out)->kind = TABLE_LIST;
    return OK;
}

static STATUS_MUST_USE Status host_zig_map(Run *r, Pos pos, Table **out) {
    TRY(run_new_table(r, pos, out));
    (*out)->kind = TABLE_MAP;
    return OK;
}

static STATUS_MUST_USE Status host_zig_push_table(Run *r, Pos pos, Table *list, Table *t) {
    Value v;
    value_table(&v, t);
    return run_list_push(r, pos, list, &v);
}

static STATUS_MUST_USE Status host_zig_put_table(Run *r, Pos pos, Table *m, Str k, Table *t) {
    Value v;
    value_table(&v, t);
    return run_map_put(r, pos, m, k, &v);
}

// A field of the answer, kept, put in `m` under `k`.
static STATUS_MUST_USE Status host_zig_put_kept(Run *r, Pos pos, Table *m, Str k, Str s) {
    Str kept = {0};
    TRY(host_zig_keep(r, pos, s, &kept));
    return run_map_put_str(r, pos, m, k, kept);
}

