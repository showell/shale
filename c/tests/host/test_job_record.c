// The job record's tests, with Zig Shale's record tests' inputs and answers,
// and the record's text held to Zig's byte for byte.
#include "test.h"

#include "../../host/job_record.c"

// `job_record_parse` of a literal, through a copy it may unescape in place.
static bool parse_copy(Str s, JobRecord *r) {
    static char b[1024];
    Bytes room = {b, sizeof b};
    Bytes text = SLICE_SUB(Bytes, room, 0, s.len);
    if (s.len > 0) mem_copy(text.ptr, s.ptr, s.len);
    return job_record_parse(text, r);
}

static void record_of(JobRecord *r, Str argv) {
    memset(r, 0, sizeof *r);
    r->pid = 42;
    r->pgid = 42;
    r->start = S("123");
    r->started = 9;
    r->argv = argv;
    r->reaper = 1;
    r->reaper_start = S("5");
}

TEST(a_record_reads_back_as_it_was_written_and_a_broken_one_is_none) {
    char buf_bytes[512];
    Bytes buf = {buf_bytes, sizeof buf_bytes};
    JobRecord w;
    memset(&w, 0, sizeof w);
    w.pid = 42;
    w.pgid = 42;
    w.start = S("123");
    w.started = 9;
    w.argv = S("zig build");
    w.has_exit = true;
    w.exit = 3;
    w.has_ended = true;
    w.ended = 11;
    Str text;
    EXPECT(job_record_format(buf, w, &text));
    // Zig's text for the same record, exactly.
    EXPECT_STR(text, S("pid=42\npgid=42\nstart=123\nstarted=9\nargv=zig build\nreaper=0\nreaper_start=\nexit=3\nended=11\n"));
    Bytes mine = {buf_bytes, text.len};
    JobRecord r;
    EXPECT(job_record_parse(mine, &r));
    EXPECT_EQ_I(r.pid, 42);
    EXPECT_STR(r.start, S("123"));
    EXPECT(r.has_exit);
    EXPECT_EQ(r.exit, 3);
    EXPECT(job_record_done(r));
    EXPECT(!parse_copy(S("pid=x\n"), &r));
    EXPECT(!parse_copy(S(""), &r));
    // An empty text that holds NULL is none too, and never reaches memchr.
    Bytes none = {NULL, 0};
    EXPECT(!job_record_parse(none, &r));
    // A record missing a key, holding one it does not know, or a line that is
    // no `key=value`, is none: never a record with a key read as empty.
    EXPECT(!parse_copy(S("pid=42\npgid=42\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
    EXPECT(!parse_copy(S("pid=42\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\nexitt=3\n"), &r));
    EXPECT(!parse_copy(S("pid=42\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\ngarbage\n"), &r));
}

TEST(an_argv_with_a_newline_or_a_backslash_reads_back_and_none_overrides_a_key) {
    char buf_bytes[512];
    Bytes buf = {buf_bytes, sizeof buf_bytes};
    static const Str argvs_all[3] = {S("git commit -m one\ntwo"), S("x\npid=7\nexit=3"), S("a\\nb \\ c")};
    SLICE_TYPE(Argvs, const Str);
    Argvs argvs = {argvs_all, 3};
    for (size_t i = 0; i < argvs.len; i++) {
        Str text;
        JobRecord w;
        record_of(&w, at(argvs, i));
        EXPECT(job_record_format(buf, w, &text));
        Bytes mine = {buf_bytes, text.len};
        JobRecord r;
        EXPECT(job_record_parse(mine, &r));
        EXPECT_STR(r.argv, at(argvs, i));
        EXPECT_EQ_I(r.pid, 42);
        EXPECT(!job_record_done(r));
    }
    JobRecord r;
    EXPECT(!parse_copy(S("pid=42\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\npid=7\n"), &r));
    // A key other than the pid's twice, which the pgid rule alone would not
    // refuse.
    EXPECT(!parse_copy(S("pid=42\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\nstarted=8\n"), &r));
    EXPECT(!parse_copy(S("pid=42\npgid=42\nstart=1\nstarted=9\nargv=a\\qb\nreaper=1\nreaper_start=5\n"), &r));
    // A record too big for its buffer is none, never one cut short.
    char small_bytes[64];
    Bytes small = {small_bytes, sizeof small_bytes};
    char long_bytes[64];
    memset(long_bytes, 120, sizeof long_bytes);
    Str text;
    JobRecord big;
    record_of(&big, (Str){long_bytes, sizeof long_bytes});
    EXPECT(!job_record_format(small, big, &text));
}

TEST(the_record_escapes_its_text_and_fits_its_buffer_exactly) {
    char buf_bytes[512];
    Bytes buf = {buf_bytes, sizeof buf_bytes};
    JobRecord w;
    record_of(&w, S("a\\nb \\ c\nd"));
    w.has_signal = true;
    w.signal = 9;
    Str text;
    EXPECT(job_record_format(buf, w, &text));
    EXPECT_STR(text, S("pid=42\npgid=42\nstart=123\nstarted=9\nargv=a\\\\nb \\\\ c\\nd\nreaper=1\nreaper_start=5\nsignal=9\n"));
    // A buffer one byte short of the text is refused, and one exactly its
    // size holds it.
    Bytes exact = {buf_bytes, text.len};
    Bytes short_by_one = {buf_bytes, text.len - 1};
    Str again;
    EXPECT(!job_record_format(short_by_one, w, &again));
    EXPECT(job_record_format(exact, w, &again));
    EXPECT_EQ(again.len, exact.len);
    // INT64_MIN and the largest u8 and u32 print in plain decimal.
    w.started = INT64_MIN;
    w.has_exit = true;
    w.exit = 255;
    w.signal = UINT32_MAX;
    Bytes all = {buf_bytes, sizeof buf_bytes};
    EXPECT(job_record_format(all, w, &again));
    EXPECT_STR(again, S("pid=42\npgid=42\nstart=123\nstarted=-9223372036854775808\nargv=a\\\\nb \\\\ c\\nd\nreaper=1\nreaper_start=5\nexit=255\nsignal=4294967295\n"));
}

TEST(a_number_takes_a_sign_and_inner_underscores_and_nothing_else) {
    JobRecord r;
    // `+`, `_` between digits and `-0` are Zig's; `_` first or last, a
    // space, an empty value and a value past its type are not.
    EXPECT(parse_copy(S("pid=+4_2\npgid=42\nstart=1\nstarted=-9_0\nargv=x\nreaper=-1\nreaper_start=5\nexit=-0\n"), &r));
    EXPECT_EQ_I(r.pid, 42);
    EXPECT_EQ_I(r.started, -90);
    EXPECT_EQ_I(r.reaper, -1);
    EXPECT(r.has_exit);
    EXPECT_EQ(r.exit, 0);
    EXPECT(!parse_copy(S("pid=_42\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
    EXPECT(!parse_copy(S("pid=42_\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
    EXPECT(!parse_copy(S("pid= 42\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
    EXPECT(!parse_copy(S("pid=\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
    EXPECT(!parse_copy(S("pid=-\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
    EXPECT(!parse_copy(S("pid=2147483648\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
    EXPECT(parse_copy(S("pid=2147483647\npgid=2147483647\nstart=1\nstarted=-9223372036854775808\nargv=x\nreaper=-2147483648\nreaper_start=5\n"), &r));
    EXPECT_EQ_I(r.pid, 2147483647);
    EXPECT_EQ_I(r.started, INT64_MIN);
    EXPECT(!parse_copy(S("pid=42\npgid=42\nstart=1\nstarted=-9223372036854775809\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
    EXPECT(!parse_copy(S("pid=42\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\nexit=256\n"), &r));
    EXPECT(!parse_copy(S("pid=42\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\nexit=-1\n"), &r));
    EXPECT(!parse_copy(S("pid=42\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\nsignal=4294967296\n"), &r));
    // A pid or pgid that is not positive is none; a `start` may hold `=`.
    EXPECT(!parse_copy(S("pid=0\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
    EXPECT(!parse_copy(S("pid=42\npgid=-3\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
    EXPECT(parse_copy(S("pid=42\npgid=42\nstart=a=b\nstarted=9\nargv=x\nreaper=1\nreaper_start=5"), &r));
    EXPECT_STR(r.start, S("a=b"));
    // A trailing backslash in argv is no escape.
    EXPECT(!parse_copy(S("pid=42\npgid=42\nstart=1\nstarted=9\nargv=x\\\nreaper=1\nreaper_start=5\n"), &r));
}

TEST(a_record_whose_pgid_is_not_its_pid_is_none_so_no_other_group_is_signalled) {
    JobRecord r;
    EXPECT(parse_copy(S("pid=42\npgid=42\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
    EXPECT_EQ_I(r.pgid, 42);
    EXPECT(!parse_copy(S("pid=42\npgid=43\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
    EXPECT(!parse_copy(S("pid=42\npgid=1\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
    EXPECT(!parse_copy(S("pid=0\npgid=0\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
    EXPECT(!parse_copy(S("pid=-5\npgid=-5\nstart=1\nstarted=9\nargv=x\nreaper=1\nreaper_start=5\n"), &r));
}

TEST_MAIN(TEST_CASE(a_record_reads_back_as_it_was_written_and_a_broken_one_is_none),
          TEST_CASE(an_argv_with_a_newline_or_a_backslash_reads_back_and_none_overrides_a_key),
          TEST_CASE(the_record_escapes_its_text_and_fits_its_buffer_exactly), TEST_CASE(a_number_takes_a_sign_and_inner_underscores_and_nothing_else),
          TEST_CASE(a_record_whose_pgid_is_not_its_pid_is_none_so_no_other_group_is_signalled))
