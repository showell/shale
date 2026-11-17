// job_record.c: a job's record, `<log>.job`: `key=value` lines a job's
// reaper writes and every later `shale` reads to find the job again. It is
// plain text and makes no system call; the processes a record names are
// jobs.c's to signal.

#include "slice.h"

// A job's record, as `<log>.job` holds it. The strings point into the text
// it was read from.
typedef struct {
    // The job's leader.
    int32_t pid;
    // The job's process group, which is its session's.
    int32_t pgid;
    // The leader's start time, raw from `/proc/<pid>/stat`.
    Str start;
    // When the job started, in Unix seconds.
    int64_t started;
    // The job's argv joined by spaces, for display.
    Str argv;
    // The reaper's pid.
    int32_t reaper;
    // The reaper's start time, raw from `/proc/<pid>/stat`.
    Str reaper_start;
    // The exit code, once the job exited.
    bool has_exit;
    uint8_t exit;
    // The signal that ended the job, once it did.
    bool has_signal;
    uint32_t signal;
    // When the job ended, in Unix seconds.
    bool has_ended;
    int64_t ended;
} JobRecord;

// The most bytes a job's argv may take in its record, escaped: the rest of
// the jobs' 8 KiB record holds the other keys.
enum { JOB_RECORD_ARGV_MOST = 7168 };

static bool job_record_done(JobRecord r) {
    return r.has_exit || r.has_signal;
}

// `s` in base 10 as Zig's `std.fmt.parseInt` reads it, into `*out`, held to
// `[min, max]`: one `+` or `-` first, then digits with `_` between them
// (never first or last). False on any other byte, on an empty number, and
// past the range, which is Zig's `Overflow`: `-0` is 0 even for an unsigned
// type, and `-1` is refused there. The magnitude only grows digit by digit,
// so a check of the end is a check of every step Zig makes.
static bool job_record_int(Str s, int64_t min, int64_t max, int64_t *out) {
    if (s.len == 0) return false;
    bool neg = false;
    size_t from = 0;
    if (at(s, 0) == '+' || at(s, 0) == '-') {
        neg = at(s, 0) == '-';
        from = 1;
    }
    Str digits = str_sub(s, from, s.len);
    if (digits.len == 0) return false;
    if (at(digits, 0) == '_' || at(digits, digits.len - 1) == '_') return false;
    uint64_t mag = 0;
    for (size_t i = 0; i < digits.len; i++) {
        char c = at(digits, i);
        if (c == '_') continue;
        if (c < '0' || c > '9') return false;
        uint64_t d = (uint64_t)(c - '0');
        if (mag > (UINT64_MAX - d) / 10) return false;
        mag = mag * 10 + d;
    }
    if (neg) {
        // -(min + 1) + 1 is |min| without overflowing it.
        uint64_t most = min < 0 ? (uint64_t)(-(min + 1)) + 1 : 0;
        if (mag > most) return false;
        *out = mag == 0 ? 0 : -(int64_t)(mag - 1) - 1;
        return true;
    }
    if (mag > (uint64_t)max) return false;
    *out = (int64_t)mag;
    return true;
}

// `argv` as `job_record_format` escapes it, unescaped in place: `\n` a
// newline, `\\` a backslash. False on any other escape or a lone `\` last.
static bool job_record_unescape(Bytes v, Str *out) {
    size_t o = 0;
    for (size_t i = 0; i < v.len; i++) {
        char c = at(v, i);
        if (c == '\\') {
            i++;
            if (i == v.len) return false;
            char e = at(v, i);
            if (e == 'n') {
                c = '\n';
            } else if (e == '\\') {
                c = '\\';
            } else {
                return false;
            }
        }
        at(v, o) = c;
        o++;
    }
    out->ptr = v.ptr;
    out->len = o;
    return true;
}

// Reads a record's text into `*r`, its strings pointing into `text`. False
// if it is not a record: one of the first seven keys missing (the format
// writes each), a key it does not know or twice, a line that is no
// `key=value`, a number Zig would not read, an `argv` escaped wrongly, or a
// `pgid` that is not the `pid`. A missing `start` read as "" would make
// `job_alive` say false of a job that runs. `argv` is unescaped in place, so
// `text` is changed.
static bool job_record_parse(Bytes text, JobRecord *r) {
    static const Str keys[] = {
        S_ITEM("pid"), S_ITEM("pgid"), S_ITEM("start"), S_ITEM("started"), S_ITEM("argv"),
        S_ITEM("reaper"), S_ITEM("reaper_start"), S_ITEM("exit"), S_ITEM("signal"), S_ITEM("ended"),
    };
    enum { JOB_RECORD_KEYS = 10, REQUIRED = 7 };
    // Sized by its keys: the cases below are their places, so a key left
    // out must not compile.
    _Static_assert(sizeof keys / sizeof *keys == JOB_RECORD_KEYS, "a key for every field of a record");
    SLICE_TYPE(Keys, const Str);
    Keys all = {keys, JOB_RECORD_KEYS};
    bool seen_bits[JOB_RECORD_KEYS] = {false};
    SLICE_TYPE(Seen, bool);
    Seen seen = {seen_bits, JOB_RECORD_KEYS};
    memset(r, 0, sizeof *r);
    size_t line_at = 0;
    while (line_at <= text.len) {
        Bytes rest = SLICE_SUB(Bytes, text, line_at, text.len);
        const char *nl = str_find_byte(((Str){rest.ptr, rest.len}), '\n');
        size_t len = nl != NULL ? (size_t)(nl - rest.ptr) : rest.len;
        Bytes line = SLICE_SUB(Bytes, rest, 0, len);
        line_at += len + 1;
        if (line.len == 0) continue;
        const char *eq = str_find_byte(((Str){line.ptr, line.len}), '=');
        if (eq == NULL) return false;
        size_t eq_at = (size_t)(eq - line.ptr);
        Str k = {line.ptr, eq_at};
        Bytes v_bytes = SLICE_SUB(Bytes, line, eq_at + 1, line.len);
        Str v = {v_bytes.ptr, v_bytes.len};
        size_t i = 0;
        while (i < all.len && !str_eq(k, at(all, i))) i++;
        if (i == all.len) return false;
        if (at(seen, i)) return false;
        at(seen, i) = true;
        int64_t n = 0;
        switch (i) {
        case 0:
            if (!job_record_int(v, INT32_MIN, INT32_MAX, &n)) return false;
            r->pid = (int32_t)n;
            break;
        case 1:
            if (!job_record_int(v, INT32_MIN, INT32_MAX, &n)) return false;
            r->pgid = (int32_t)n;
            break;
        case 2:
            r->start = v;
            break;
        case 3:
            if (!job_record_int(v, INT64_MIN, INT64_MAX, &n)) return false;
            r->started = n;
            break;
        case 4:
            if (!job_record_unescape(v_bytes, &r->argv)) return false;
            break;
        case 5:
            if (!job_record_int(v, INT32_MIN, INT32_MAX, &n)) return false;
            r->reaper = (int32_t)n;
            break;
        case 6:
            r->reaper_start = v;
            break;
        case 7:
            if (!job_record_int(v, 0, UINT8_MAX, &n)) return false;
            r->has_exit = true;
            r->exit = (uint8_t)n;
            break;
        case 8:
            if (!job_record_int(v, 0, UINT32_MAX, &n)) return false;
            r->has_signal = true;
            r->signal = (uint32_t)n;
            break;
        default:
            if (!job_record_int(v, INT64_MIN, INT64_MAX, &n)) return false;
            r->has_ended = true;
            r->ended = n;
            break;
        }
    }
    for (size_t i = 0; i < REQUIRED; i++) {
        if (!at(seen, i)) return false;
    }
    // The reaper starts the job as its group's leader, so it writes the two
    // the same: a record where they differ names some other group, a stale
    // or forged record's, and what reads it would signal that group.
    return r->pid > 0 && r->pgid == r->pid;
}

// A writer into a fixed buffer that remembers whether anything did not fit.
typedef struct {
    Bytes room;
    size_t len;
    bool over;
} JobRecordOut;

static void job_record_put(JobRecordOut *w, Str s) {
    if (w->over || s.len > w->room.len - w->len) {
        w->over = true;
        return;
    }
    Bytes dest = SLICE_SUB(Bytes, w->room, w->len, w->len + s.len);
    if (s.len > 0) memcpy(dest.ptr, s.ptr, s.len);
    w->len += s.len;
}

// `n` in decimal, as Zig's `{d}` prints it.
static void job_record_put_int(JobRecordOut *w, int64_t n) {
    char digits[21];
    Bytes d = {digits, sizeof digits};
    size_t k = d.len;
    // -(n + 1) + 1 is |n| without overflowing INT64_MIN.
    uint64_t mag = n < 0 ? (uint64_t)(-(n + 1)) + 1 : (uint64_t)n;
    do {
        k--;
        at(d, k) = (char)('0' + mag % 10);
        mag /= 10;
    } while (mag > 0);
    if (n < 0) {
        k--;
        at(d, k) = '-';
    }
    job_record_put(w, str_sub(((Str){digits, sizeof digits}), k, sizeof digits));
}

// The record's text in `buf`, `argv` escaped, into `*out`; false if it does
// not fit, never a record cut short.
static bool job_record_format(Bytes buf, JobRecord r, Str *out) {
    JobRecordOut w = {buf, 0, false};
    job_record_put(&w, S("pid="));
    job_record_put_int(&w, r.pid);
    job_record_put(&w, S("\npgid="));
    job_record_put_int(&w, r.pgid);
    job_record_put(&w, S("\nstart="));
    job_record_put(&w, r.start);
    job_record_put(&w, S("\nstarted="));
    job_record_put_int(&w, r.started);
    job_record_put(&w, S("\nargv="));
    for (size_t i = 0; i < r.argv.len; i++) {
        char c = at(r.argv, i);
        if (c == '\\') {
            job_record_put(&w, S("\\\\"));
        } else if (c == '\n') {
            job_record_put(&w, S("\\n"));
        } else {
            job_record_put(&w, str_sub(r.argv, i, i + 1));
        }
    }
    job_record_put(&w, S("\nreaper="));
    job_record_put_int(&w, r.reaper);
    job_record_put(&w, S("\nreaper_start="));
    job_record_put(&w, r.reaper_start);
    job_record_put(&w, S("\n"));
    if (r.has_exit) {
        job_record_put(&w, S("exit="));
        job_record_put_int(&w, r.exit);
        job_record_put(&w, S("\n"));
    }
    if (r.has_signal) {
        job_record_put(&w, S("signal="));
        job_record_put_int(&w, r.signal);
        job_record_put(&w, S("\n"));
    }
    if (r.has_ended) {
        job_record_put(&w, S("ended="));
        job_record_put_int(&w, r.ended);
        job_record_put(&w, S("\n"));
    }
    if (w.over) return false;
    out->ptr = buf.ptr;
    out->len = w.len;
    return true;
}
