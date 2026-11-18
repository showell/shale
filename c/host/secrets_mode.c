// secrets_mode.c: `shale --secrets`, three commands for a person at a
// terminal, over the mounts marked ` secrets`:
//
//   1. where: each secrets mount's name and folder;
//   2. store: a new secret, its value read from the keyboard with echo off
//      and written as `run`'s `stdout_secret` writes one (swap_file.c), its
//      name checked as that target is (paths_secret_target);
//   3. deny: the rules that keep Claude Code's Read and Edit out of each
//      secrets folder, added to `~/.claude/settings.json`'s
//      `permissions.deny` (settings_json.c).
//
// It refuses to start unless stdin and stdout are both a terminal, so an
// agent's shell, whose stdin and stdout are pipes, cannot drive it; the gate
// refuses `shale --secrets` too, since it is not an option the gate passes.
// Nothing it prints holds a secret's value.
//
// Include file_system.c, path_run.c, mounts.c, paths.c, swap_file.c and
// settings_json.c first.

#include <signal.h>
#include <termios.h>

// The most bytes a stored secret may have: `stdout_secret`'s cap.
enum { SECRETS_VALUE_MAX = 64 * 1024 };

// The most bytes an answer other than a value may have.
enum { SECRETS_ANSWER_MAX = 1024 };

// A rule's glob for a folder and everything under it, `/` then two `*`
// and the rule's `)`, spelled with escapes: the discipline check takes a `/`
// followed by a `*` for the start of a block comment.
#define SECRETS_UNDER "/\x2a\x2a)"

// The most bytes of a settings file it reads.
enum { SECRETS_SETTINGS_MAX = 16 << 20 };

// The mode's exit codes: done (or declined), failed, refused.
enum { SECRETS_DONE = 0, SECRETS_FAILED = 1, SECRETS_REFUSED = 2 };

// Where the mode talks: the keyboard, its answers, and its refusals.
typedef struct {
    int in;
    int out;
    int err;
} SecretsIo;

// What the mode works over.
typedef struct {
    // The CLI's arena: messages, paths and the settings tree.
    Arena *arena;
    SecretsIo io;
    // $HOME, where `.claude/settings.json` is.
    Str home;
    // The process's working folder, absolute: the paths check's root.
    Str cwd;
    MountsItems mounts;
    Str mounts_from;
} Secrets;

static void secrets_put(int fd, Str s) {
    size_t done = 0;
    while (done < s.len) {
        ssize_t n = write(fd, &at(s, done), s.len - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        done += (size_t)n;
    }
}

// An errno by the store's name for it (`FileNotFound`, `AccessDenied`).
static Str secrets_errno_name(int e) {
    return file_system_err_name(file_system_err_of(e));
}

// Reads all of the file at `path`, at most `limit` bytes: "" in `*why`, or
// why not.
static void secrets_read_file(Arena *a, Str path, size_t limit, Str *out, Str *why) {
    *why = S("");
    FileSystemCPath cp;
    const char *c = NULL;
    // The path is made from HOME, which the kernel handed over NUL-ended.
    if (file_system_cstr(&cp, os_str_own(path), &c) != FILE_SYSTEM_OK) {
        *why = S("its path is too long");
        return;
    }
    int fd = open(c, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        *why = secrets_errno_name(errno);
        return;
    }
    Buf b = buf_make(a);
    char chunk[16384];
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) {
            *why = secrets_errno_name(errno);
            break;
        }
        if (n == 0) break;
        Str got = {chunk, (size_t)n};
        if (b.len + got.len > limit) {
            *why = S("past 16 MiB");
            break;
        }
        if (buf_str(&b, got) != OK) {
            *why = S("out of memory");
            break;
        }
    }
    close(fd);
    *out = buf_view(&b);
}

static void secrets_say(Secrets *m, Str s) {
    secrets_put(m->io.out, s);
}

// A refusal or a failure on the error stream, `shale --secrets: <why>`,
// giving `code`.
static int secrets_fail(Secrets *m, Str why, int code) {
    secrets_put(m->io.err, S("shale --secrets: "));
    secrets_put(m->io.err, why);
    secrets_put(m->io.err, S("\n"));
    return code;
}

// `a`, `b` and `c` joined in the mode's arena; "" when it refused.
static Str secrets_cat(Secrets *m, Str a, Str b, Str c) {
    Buf w = buf_make(m->arena);
    if (buf_str(&w, a) != OK || buf_str(&w, b) != OK || buf_str(&w, c) != OK) return S("");
    return buf_view(&w);
}

// Whether `io`'s stdin and stdout are both a terminal: the mode's second
// lock, after the gate's.
static bool secrets_on_terminal(SecretsIo io) {
    return isatty(io.in) && isatty(io.out);
}

static int secrets_refuse_terminal(SecretsIo io) {
    secrets_put(io.err, S("shale --secrets: run it in your own terminal: it reads a secret from the keyboard, so its stdin and its stdout must both be a terminal\n"));
    return SECRETS_REFUSED;
}

typedef enum {
    SECRETS_LINE_OK,
    // The input ended before a line did.
    SECRETS_LINE_ENDED,
    // The line was longer than its room; it was read through and dropped.
    SECRETS_LINE_LONG,
    SECRETS_LINE_FAILED,
} SecretsLine;

// One line from `fd` into `room`, without its newline (or a `\r` before it),
// its length in `*len`. Read a byte at a time, so nothing after the line is
// taken from the terminal.
static SecretsLine secrets_read_line(int fd, Bytes room, size_t *len) {
    *len = 0;
    bool long_line = false;
    bool any = false;
    for (;;) {
        char c = 0;
        ssize_t n = read(fd, &c, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return SECRETS_LINE_FAILED;
        if (n == 0) {
            if (!any) return SECRETS_LINE_ENDED;
            break;
        }
        any = true;
        if (c == '\n') break;
        if (*len >= room.len) {
            long_line = true;
            continue;
        }
        at(room, *len) = c;
        (*len)++;
    }
    if (long_line) return SECRETS_LINE_LONG;
    if (*len > 0 && at(room, *len - 1) == '\r') (*len)--;
    return SECRETS_LINE_OK;
}

// An answer to a question, spaces trimmed, into `*out` (in the arena): false
// when the input ended or failed, said on the error stream.
static bool secrets_ask(Secrets *m, Str question, Str *out) {
    secrets_say(m, question);
    char bytes[SECRETS_ANSWER_MAX];
    Bytes room = {bytes, sizeof bytes};
    size_t len = 0;
    SecretsLine got = secrets_read_line(m->io.in, room, &len);
    if (got != SECRETS_LINE_OK) {
        secrets_say(m, S("\n"));
        (void)secrets_fail(m, got == SECRETS_LINE_LONG ? S("the answer is too long") : S("no answer"), SECRETS_FAILED);
        return false;
    }
    Str s = {bytes, len};
    while (s.len > 0 && str_byte(s, 0) == ' ') s = str_sub(s, 1, s.len);
    while (s.len > 0 && str_byte(s, s.len - 1) == ' ') s = str_sub(s, 0, s.len - 1);
    if (!arena_dup(m->arena, s, out)) *out = S("");
    return true;
}

// A yes-or-no question: `y` or `yes` is yes, and any other answer no.
static bool secrets_yes(Secrets *m, Str question, bool *answered) {
    Str a = S("");
    *answered = secrets_ask(m, question, &a);
    return *answered && (str_eq(a, S("y")) || str_eq(a, S("Y")) || str_eq(a, S("yes")));
}

// ── where ──

static int secrets_where(Secrets *m) {
    for (size_t k = 0; k < m->mounts.len; k++) {
        const Mount *mt = &at(m->mounts, k);
        if (!mt->secrets) continue;
        secrets_say(m, secrets_cat(m, S("  "), mt->name, S(": ")));
        secrets_say(m, secrets_cat(m, mt->dir, S("\n"), S("")));
    }
    return SECRETS_DONE;
}

// ── deny ──

// The deny rules for every secrets mount, Read's then Edit's for each: its
// folder and everything under it, as Claude Code's permission rules write
// an absolute path (`//` then the path without its leading `/`).
static STATUS_MUST_USE Status secrets_deny_rules(Secrets *m, SettingsStrs *out) {
    *out = SettingsStrs_make(m->arena);
    for (size_t k = 0; k < m->mounts.len; k++) {
        const Mount *mt = &at(m->mounts, k);
        if (!mt->secrets) continue;
        Str dir = mounts_trim_slash(mt->dir);
        // The filesystem's root is two slashes then the glob, never three.
        if (str_eq(dir, S("/"))) dir = S("");
        for (int j = 0; j < 2; j++) {
            Str rule = secrets_cat(m, j == 0 ? S("Read(/") : S("Edit(/"), dir, S(SECRETS_UNDER));
            if (rule.len == 0) return STOP;
            TRY(SettingsStrs_push(out, rule));
        }
    }
    return OK;
}

// The settings file read and judged: its tree, its bytes as read, whether it
// was there, and the rules it lacks.
typedef struct {
    Str path;
    Str bak;
    bool existed;
    Str text;
    SettingsNode *root;
    SettingsDenies denies;
} SecretsSettings;

#define SECRETS_SETTINGS_SHOWN "~/.claude/settings.json"

// Reads `~/.claude/settings.json` and finds the deny rules it lacks: a
// missing file is an empty object. On a file that does not read, does not
// parse or cannot hold the rules it says why and gives false.
static bool secrets_settings(Secrets *m, SecretsSettings *s) {
    memset(s, 0, sizeof *s);
    s->path = secrets_cat(m, m->home, S("/.claude/settings.json"), S(""));
    s->bak = secrets_cat(m, s->path, S(".bak"), S(""));
    FileSystemCPath cp;
    const char *c = NULL;
    if (s->path.len == 0 || s->bak.len == 0) {
        (void)secrets_fail(m, S(SECRETS_SETTINGS_SHOWN ": out of memory"), SECRETS_FAILED);
        return false;
    }
    if (file_system_cstr(&cp, os_str_own(s->path), &c) != FILE_SYSTEM_OK) {
        (void)secrets_fail(m, S(SECRETS_SETTINGS_SHOWN ": its path is too long"), SECRETS_FAILED);
        return false;
    }
    struct stat st;
    if (lstat(c, &st) == 0) {
        if (!S_ISREG(st.st_mode)) {
            (void)secrets_fail(m, S(SECRETS_SETTINGS_SHOWN " is not a plain file (a link, a folder): change it by hand; nothing is written"), SECRETS_FAILED);
            return false;
        }
        Str why = S("");
        secrets_read_file(m->arena, s->path, SECRETS_SETTINGS_MAX, &s->text, &why);
        if (why.len > 0) {
            (void)secrets_fail(m, secrets_cat(m, S(SECRETS_SETTINGS_SHOWN " cannot be read ("), why, S("): nothing is written")), SECRETS_FAILED);
            return false;
        }
        s->existed = true;
    } else if (errno == ENOENT) {
        s->text = S("{}");
    } else {
        (void)secrets_fail(m, secrets_cat(m, S(SECRETS_SETTINGS_SHOWN " cannot be looked at ("), secrets_errno_name(errno), S("): nothing is written")), SECRETS_FAILED);
        return false;
    }
    SettingsReader r;
    if (settings_read(&r, m->arena, s->text, &s->root) != OK) {
        Buf w = buf_make(m->arena);
        Status built = buf_lit(&w, SECRETS_SETTINGS_SHOWN " does not parse: ");
        if (built == OK) built = buf_str(&w, r.why.len > 0 ? r.why : S("out of memory"));
        if (built == OK) built = buf_lit(&w, " at byte ");
        if (built == OK) built = buf_u64(&w, r.why_at);
        if (built == OK) built = buf_lit(&w, "; nothing is written");
        (void)secrets_fail(m, built == OK ? buf_view(&w) : S(SECRETS_SETTINGS_SHOWN " does not parse; nothing is written"), SECRETS_FAILED);
        return false;
    }
    SettingsStrs rules;
    Str why = S("");
    if (secrets_deny_rules(m, &rules) != OK || settings_find_denies(m->arena, s->root, SettingsStrs_items(&rules), &s->denies, &why) != OK) {
        (void)secrets_fail(m, secrets_cat(m, S(SECRETS_SETTINGS_SHOWN " cannot hold the deny rules: "), why.len > 0 ? why : S("out of memory"), S("; nothing is written")), SECRETS_FAILED);
        return false;
    }
    return true;
}

// Writes `bytes` to `path` by a swap, replacing what is there: "" once
// written, else why not.
static Str secrets_swap(Secrets *m, Str path, Str bytes) {
    SwapFile f;
    if (swap_file_target(&f, os_str_own(path)) != FILE_SYSTEM_OK) return S("its path is too long");
    int err = 0;
    if (swap_file_there(&f, &err) == SWAP_NOT_FILE) return S("it is there and is not a plain file");
    int made = swap_file_open(&f, m->arena);
    if (made != 0) return made == SWAP_NO_ROOM ? S("out of memory") : secrets_cat(m, S("the file beside it cannot be made ("), secrets_errno_name(made), S(")"));
    err = swap_file_write(&f, bytes);
    if (err != 0) {
        swap_file_discard(&f);
        return secrets_cat(m, S("it cannot be written ("), secrets_errno_name(err), S(")"));
    }
    SwapEnd end = swap_file_commit(&f, true, &err);
    if (end != SWAP_DONE) {
        swap_file_discard(&f);
        return end == SWAP_UNSYNCED ? S("it cannot be synced") : secrets_cat(m, S("it cannot be renamed into place ("), secrets_errno_name(err), S(")"));
    }
    return S("");
}

// Shows the rules `s` lacks, asks, and writes them: the old file kept as
// settings.json.bak first, the new one written by a swap.
static int secrets_add_denies(Secrets *m, SecretsSettings *s) {
    secrets_say(m, S("These rules keep Claude Code's Read and Edit out of the secrets folders; they go in " SECRETS_SETTINGS_SHOWN "'s `permissions.deny`:\n"));
    for (size_t k = 0; k < s->denies.missing.len; k++) secrets_say(m, secrets_cat(m, S("  "), at(s->denies.missing, k), S("\n")));
    bool answered = false;
    if (!secrets_yes(m, S("Add them? [y/n] "), &answered)) {
        if (!answered) return SECRETS_FAILED;
        secrets_say(m, S("Nothing is written.\n"));
        return SECRETS_DONE;
    }
    size_t added = s->denies.missing.len;
    Str text = S("");
    if (settings_add_denies(m->arena, s->root, &s->denies) != OK || settings_write(m->arena, s->root, &text) != OK) {
        return secrets_fail(m, S(SECRETS_SETTINGS_SHOWN ": out of memory; nothing is written"), SECRETS_FAILED);
    }
    if (s->existed) {
        Str why = secrets_swap(m, s->bak, s->text);
        if (why.len > 0) return secrets_fail(m, secrets_cat(m, S(SECRETS_SETTINGS_SHOWN ".bak: "), why, S("; nothing is written")), SECRETS_FAILED);
    } else {
        // A first settings file may need its folder.
        Str dir = secrets_cat(m, m->home, S("/.claude"), S(""));
        if (dir.len == 0) return secrets_fail(m, S("~/.claude: out of memory; nothing is written"), SECRETS_FAILED);
        FileSystemCPath cp;
        const char *c = NULL;
        if (file_system_cstr(&cp, os_str_own(dir), &c) != FILE_SYSTEM_OK) return secrets_fail(m, S("~/.claude: its path is too long; nothing is written"), SECRETS_FAILED);
        if (mkdir(c, 0700) != 0 && errno != EEXIST) {
            return secrets_fail(m, secrets_cat(m, S("~/.claude cannot be made ("), secrets_errno_name(errno), S("); nothing is written")), SECRETS_FAILED);
        }
    }
    Str why = secrets_swap(m, s->path, text);
    if (why.len > 0) return secrets_fail(m, secrets_cat(m, S(SECRETS_SETTINGS_SHOWN ": "), why, S("; nothing is written")), SECRETS_FAILED);
    Buf w = buf_make(m->arena);
    Status built = buf_lit(&w, "Added ");
    if (built == OK) built = buf_u64(&w, added);
    if (built == OK) built = buf_str(&w, added == 1 ? S(" rule to " SECRETS_SETTINGS_SHOWN) : S(" rules to " SECRETS_SETTINGS_SHOWN));
    if (built == OK) built = buf_str(&w, s->existed ? S(" (the old file is " SECRETS_SETTINGS_SHOWN ".bak).\n") : S(".\n"));
    secrets_say(m, built == OK ? buf_view(&w) : S("Added the rules.\n"));
    return SECRETS_DONE;
}

static int secrets_deny(Secrets *m) {
    SecretsSettings s;
    if (!secrets_settings(m, &s)) return SECRETS_FAILED;
    if (s.denies.missing.len == 0) {
        secrets_say(m, S(SECRETS_SETTINGS_SHOWN " holds every deny rule already: nothing is written.\n"));
        return SECRETS_DONE;
    }
    return secrets_add_denies(m, &s);
}

// ── store ──

static STATUS_MUST_USE Status secrets_no_charge(void *vm, RunCtx *ctx, Pos pos, uint64_t n) {
    (void)vm;
    (void)ctx;
    (void)pos;
    (void)n;
    return OK;
}

// The OS path of `shown`, `mount:NAME`, checked as `run`'s `stdout_secret`
// target is (paths_secret_target): "" in `*why` when it passes, else what is
// wrong said for a person. A name that is not a plain file name directly in
// the mount gets one plain sentence; the check's other stops (a missing
// folder, a link) are said in its own words, which name no script's option.
static void secrets_target(Secrets *m, const Mount *mt, Str name, Str shown, Str *abs, Str *why) {
    *why = S("");
    bool refused = false;
    Arena values;
    arena_init(&values, (size_t)64 << 20, &refused);
    Arena machine;
    arena_init(&machine, (size_t)16 << 20, &refused);
    RunCtx ctx = run_ctx_make(&values, &machine, budgets_default());
    Paths paths;
    paths_make(&paths, m->arena, m->cwd, m->cwd, m->mounts, m->mounts_from);
    PathHook hook;
    paths_hook(&paths, &hook);
    PathRun run = path_run_make(&ctx, m->cwd, secrets_no_charge, NULL, &hook);
    Pos pos = {1, 1};
    Str got = S("");
    if (!paths_run_folders_make(&paths, &run)) {
        *why = S("the name cannot be checked");
    } else if (paths_secret_target(&paths, &run, pos, shown, &got) != OK) {
        bool plain_name = ctx.stopped && (ctx.stop.cls == CLASS_ARGUMENT || str_find_byte(name, '/') != NULL);
        if (plain_name) {
            Buf w = buf_make(m->arena);
            Status built = buf_lit(&w, "a secret's name is a plain file name directly in the mount, like `");
            if (built == OK) built = buf_str(&w, mt->name);
            if (built == OK) built = buf_lit(&w, ":github_token`, not `");
            if (built == OK) built = buf_str(&w, name);
            if (built == OK) built = buf_byte(&w, '`');
            *why = built == OK ? buf_view(&w) : S("the name cannot be checked");
        } else if (!ctx.stopped || !arena_dup(m->arena, ctx.stop.message, why)) {
            *why = S("the name cannot be checked");
        }
    } else if (!arena_dup(m->arena, got, abs)) {
        *why = S("the name cannot be checked");
    }
    arena_deinit(&machine);
    arena_deinit(&values);
}

// The secrets mount a new secret goes in: the one there is, or the one the
// person names. NULL when the answer names none, said.
static const Mount *secrets_pick_mount(Secrets *m) {
    const Mount *only = NULL;
    size_t n = 0;
    Buf names = buf_make(m->arena);
    for (size_t k = 0; k < m->mounts.len; k++) {
        const Mount *mt = &at(m->mounts, k);
        if (!mt->secrets) continue;
        if (n > 0 && buf_lit(&names, ", ") != OK) return NULL;
        if (buf_str(&names, mt->name) != OK) return NULL;
        only = mt;
        n++;
    }
    if (n == 1) return only;
    Str a = S("");
    if (!secrets_ask(m, secrets_cat(m, S("Which mount ("), buf_view(&names), S(")? ")), &a)) return NULL;
    for (size_t k = 0; k < m->mounts.len; k++) {
        const Mount *mt = &at(m->mounts, k);
        if (mt->secrets && str_eq(mt->name, a)) return mt;
    }
    (void)secrets_fail(m, secrets_cat(m, S("`"), a, S("` is not a secrets mount")), SECRETS_FAILED);
    return NULL;
}

// The terminal's settings from before the value is read, while they are
// changed: what a signal's handler puts back. It is the process's terminal
// and no run's, so it is a static (c/check_discipline.shale's STATICS_KEPT).
typedef struct {
    // Whether `saved` is to be put back: set while echo is off.
    volatile sig_atomic_t held;
    int fd;
    struct termios saved;
} SecretsTty;

static SecretsTty *secrets_tty(void) {
    static SecretsTty tty;
    return &tty;
}

// The signals that end the mode while echo is off: each puts the terminal
// back first.
static int secrets_signal_at(size_t k) {
    return k == 0 ? SIGINT : k == 1 ? SIGTERM : SIGHUP;
}

enum { SECRETS_SIGNALS = 3 };

typedef struct sigaction SecretsAction;
_Static_assert(SECRETS_SIGNALS == 3, "secrets_read_value keeps one action for each signal");

// SIGINT, SIGTERM or SIGHUP while echo is off: the terminal's settings put
// back, then the signal raised again under its default, so the mode ends as
// that signal ends a program. tcsetattr is async-signal-safe.
static void secrets_on_signal(int sig) {
    SecretsTty *t = secrets_tty();
    if (t->held) {
        int back = tcsetattr(t->fd, TCSANOW, &t->saved);
        (void)back;
        t->held = 0;
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

// The value's bytes, read with the terminal's ICANON and ECHO off a byte at
// a time to the newline, into `room`: no line limit of the terminal's
// applies. A DEL or a backspace takes back the byte before it, and a ^D
// first ends the input. LONG past `room`, read through to the newline.
static SecretsLine secrets_read_raw(int fd, Bytes room, size_t *len) {
    *len = 0;
    bool long_line = false;
    for (;;) {
        char c = 0;
        ssize_t n = read(fd, &c, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return SECRETS_LINE_FAILED;
        if (n == 0 || (c == 4 && *len == 0 && !long_line)) return SECRETS_LINE_ENDED;
        if (c == '\n' || c == '\r') break;
        if (c == 127 || c == 8) {
            if (*len > 0) (*len)--;
            continue;
        }
        if (*len >= room.len) {
            long_line = true;
            continue;
        }
        at(room, *len) = c;
        (*len)++;
    }
    return long_line ? SECRETS_LINE_LONG : SECRETS_LINE_OK;
}

// Reads the value with the terminal's echo and line editing off, into
// `room`: its length, or a failure said and SIZE_MAX. The terminal's
// settings come back on every way out: after the read, and from a handler
// on SIGINT, SIGTERM and SIGHUP while it waits.
static size_t secrets_read_value(Secrets *m, Bytes room) {
    SecretsTty *t = secrets_tty();
    if (tcgetattr(m->io.in, &t->saved) != 0) {
        (void)secrets_fail(m, S("the terminal's echo cannot be turned off; nothing is stored"), SECRETS_FAILED);
        return SIZE_MAX;
    }
    t->fd = m->io.in;
    struct termios quiet = t->saved;
    quiet.c_lflag &= ~(tcflag_t)(ECHO | ICANON);
    SLICE_TYPE(CcRoom, cc_t);
    CcRoom cc = {quiet.c_cc, NCCS};
    at(cc, VMIN) = 1;
    at(cc, VTIME) = 0;
    // DRAGON: the secrets rule. A value typed while the terminal still
    // echoes is shown on the screen, and a terminal left with echo off by a
    // ^C hides what is typed next, a secret or not.
    // REMEDY: the handlers go on and the settings are saved before echo goes
    // off; echo goes off before the prompt is printed, so nothing typed after
    // it is echoed; and the saved settings come back after the read, or from
    // the handler (test_secrets holds both).
    // One for each of SECRETS_SIGNALS.
    SecretsAction was[3];
    SLICE_TYPE(Actions, SecretsAction);
    Actions old = {was, sizeof was / sizeof *was};
    struct sigaction on;
    memset(&on, 0, sizeof on);
    on.sa_handler = secrets_on_signal;
    sigemptyset(&on.sa_mask);
    t->held = 1;
    for (size_t k = 0; k < old.len; k++) {
        int put = sigaction(secrets_signal_at(k), &on, &at(old, k));
        (void)put;
    }
    if (tcsetattr(m->io.in, TCSAFLUSH, &quiet) != 0) {
        t->held = 0;
        for (size_t k = 0; k < old.len; k++) {
            int put = sigaction(secrets_signal_at(k), &at(old, k), NULL);
            (void)put;
        }
        (void)secrets_fail(m, S("the terminal's echo cannot be turned off; nothing is stored"), SECRETS_FAILED);
        return SIZE_MAX;
    }
    secrets_say(m, S("The value (it is not shown as you type): "));
    size_t len = 0;
    SecretsLine got = secrets_read_raw(m->io.in, room, &len);
    int back = tcsetattr(m->io.in, TCSAFLUSH, &t->saved);
    (void)back;
    t->held = 0;
    for (size_t k = 0; k < old.len; k++) {
        int put = sigaction(secrets_signal_at(k), &at(old, k), NULL);
        (void)put;
    }
    secrets_say(m, S("\n"));
    if (got == SECRETS_LINE_LONG) {
        (void)secrets_fail(m, S("the value is longer than 64 KiB, the most a secret may be; nothing is stored"), SECRETS_FAILED);
        return SIZE_MAX;
    }
    if (got != SECRETS_LINE_OK) {
        (void)secrets_fail(m, S("no value was read; nothing is stored"), SECRETS_FAILED);
        return SIZE_MAX;
    }
    return len;
}

// Stores `value` at the checked target: "" once stored, else why not. With
// `replace` false a file made there meanwhile is kept.
static Str secrets_write_value(Secrets *m, SwapFile *f, Str value, bool replace) {
    int made = swap_file_open(f, m->arena);
    if (made != 0) return made == SWAP_NO_ROOM ? S("out of memory") : secrets_cat(m, S("the file beside it cannot be made ("), secrets_errno_name(made), S(")"));
    int err = swap_file_write(f, value);
    if (err != 0) {
        swap_file_discard(f);
        return secrets_cat(m, S("it cannot be written ("), secrets_errno_name(err), S(")"));
    }
    SwapEnd end = swap_file_commit(f, replace, &err);
    if (end == SWAP_DONE) return S("");
    swap_file_discard(f);
    if (end == SWAP_UNSYNCED) return S("it cannot be synced");
    if (end == SWAP_TAKEN) return S("a file of that name was made meanwhile");
    return secrets_cat(m, S("it cannot be renamed into place ("), secrets_errno_name(err), S(")"));
}

static int secrets_store(Secrets *m) {
    const Mount *mt = secrets_pick_mount(m);
    if (mt == NULL) return SECRETS_FAILED;
    Str name = S("");
    if (!secrets_ask(m, secrets_cat(m, S("The secret's name (a file directly in `"), mt->name, S("`): ")), &name)) return SECRETS_FAILED;
    if (name.len == 0) return secrets_fail(m, S("no name was given; nothing is stored"), SECRETS_FAILED);
    Str shown = secrets_cat(m, mt->name, S(":"), name);
    Str abs = S("");
    Str why = S("");
    secrets_target(m, mt, name, shown, &abs, &why);
    if (why.len > 0) return secrets_fail(m, secrets_cat(m, why, S("; nothing is stored"), S("")), SECRETS_FAILED);
    SwapFile f;
    // Its name was held to the strict name rule (paths_secret_target).
    if (swap_file_target(&f, os_str_own(abs)) != FILE_SYSTEM_OK) return secrets_fail(m, secrets_cat(m, shown, S(": its path is too long; nothing is stored"), S("")), SECRETS_FAILED);
    int err = 0;
    SwapThere there = swap_file_there(&f, &err);
    if (there == SWAP_NOT_FILE) return secrets_fail(m, secrets_cat(m, shown, S(" is there and is not a plain file; nothing is stored"), S("")), SECRETS_FAILED);
    if (there == SWAP_UNSEEN) return secrets_fail(m, secrets_cat(m, shown, S(" cannot be looked at ("), secrets_cat(m, secrets_errno_name(err), S("); nothing is stored"), S(""))), SECRETS_FAILED);
    bool replace = false;
    if (there == SWAP_FILE) {
        bool answered = false;
        replace = secrets_yes(m, secrets_cat(m, shown, S(" is there already. Replace it? [y/n] "), S("")), &answered);
        if (!answered) return SECRETS_FAILED;
        if (!replace) {
            secrets_say(m, S("Nothing is stored.\n"));
            return SECRETS_DONE;
        }
    }
    char bytes[SECRETS_VALUE_MAX];
    Bytes room = {bytes, sizeof bytes};
    size_t len = secrets_read_value(m, room);
    Str failed = S("");
    if (len != SIZE_MAX && len > 0) {
        Str value = {bytes, len};
        failed = secrets_write_value(m, &f, value, replace);
    }
    // The value's bytes, or a part of them read before a failure, are not
    // left on the stack.
    explicit_bzero(bytes, sizeof bytes);
    if (len == SIZE_MAX) return SECRETS_FAILED;
    if (len == 0) {
        secrets_say(m, S("The value is empty: nothing is stored.\n"));
        return SECRETS_DONE;
    }
    if (failed.len > 0) return secrets_fail(m, secrets_cat(m, shown, S(": "), secrets_cat(m, failed, S("; nothing is stored"), S(""))), SECRETS_FAILED);
    Buf w = buf_make(m->arena);
    bool built = buf_lit(&w, "stored ") == OK && buf_str(&w, shown) == OK && buf_lit(&w, " (") == OK && buf_u64(&w, len) == OK && buf_lit(&w, " bytes)\n") == OK;
    secrets_say(m, built ? buf_view(&w) : S("stored\n"));
    // Then the deny rules, when any is missing.
    SecretsSettings s;
    if (!secrets_settings(m, &s)) return SECRETS_FAILED;
    if (s.denies.missing.len == 0) return SECRETS_DONE;
    return secrets_add_denies(m, &s);
}

// ── the menu ──

// `shale --secrets`: refused off a terminal and without a secrets mount,
// else the menu, then the command chosen. Gives the exit code.
static int secrets_main(Secrets *m) {
    if (!secrets_on_terminal(m->io)) return secrets_refuse_terminal(m->io);
    bool any = false;
    for (size_t k = 0; k < m->mounts.len; k++) any = any || at(m->mounts, k).secrets;
    if (!any) {
        return secrets_fail(m, secrets_cat(m, S("no mount is a `secrets` mount ("), m->mounts_from, S("): add a line such as `keys = ~/.shale-keys secrets` first")), SECRETS_REFUSED);
    }
    secrets_say(m, S("shale --secrets: the secrets mounts' commands\n"
                     "  1. where: show each secrets mount's folder\n"
                     "  2. store: store a new secret\n"
                     "  3. deny: add the deny rules to Claude Code's settings\n"));
    Str a = S("");
    if (!secrets_ask(m, S("Which one (1, 2 or 3)? "), &a)) return SECRETS_FAILED;
    if (str_eq(a, S("1")) || str_eq(a, S("where"))) return secrets_where(m);
    if (str_eq(a, S("2")) || str_eq(a, S("store"))) return secrets_store(m);
    if (str_eq(a, S("3")) || str_eq(a, S("deny"))) return secrets_deny(m);
    return secrets_fail(m, secrets_cat(m, S("`"), a, S("` is not 1, 2 or 3")), SECRETS_REFUSED);
}
