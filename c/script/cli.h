// cli.h: the `shale` command line, and the pieces of the Linux runner that need
// no system call: the usage, the refusals and their words, a script's
// `-- budget:` line, the mounts file and the report a stopped check prints.
// Every text here is in the frozen surface (docs/SURFACE.txt).

#ifndef SHALE_CLI_H
#define SHALE_CLI_H

#include "parse.h"

static inline Str cli_usage(void) {
    return S("usage: shale [--root DIR] [--verbose] [--undo-on-stop] [--check] [--budget NAME=N]... [FILE [--args-stdin] | -e SOURCE] [NAME=VALUE]...\n"
             "  --root DIR        the folder the script's paths are relative to (SHALE_ROOT, or here)\n"
             "  --check           parse and compile the script, with its preludes, and run nothing\n"
             "  --verbose         the effects, the programs run and what the run used, always\n"
             "  --undo-on-stop    a run that stops puts back every path it changed (not a program's)\n"
             "  --args-stdin      the FILE's NAME=VALUEs on stdin, one a line, each value raw to its end\n"
             "  --budget NAME=N   one budget: steps, reads, read_bytes, output_bytes, changes,\n"
             "                    change_bytes, arena_bytes, call_bytes, call_entries, call_depth,\n"
             "                    programs, kept_bytes (at most 256 MiB), machine_bytes (at most\n"
             "                    1 GiB); a script's own `-- budget: NAME=N ...` line sets its\n"
             "                    budgets where this does not\n"
             "  -e SOURCE         the script itself, in place of a file (neither: stdin is the script)\n"
             "  NAME=VALUE        a value the script reads with arg(\"NAME\", default)\n"
             "  --help            this, where the docs are, and the mounts in force (alone)\n"
             "  --version         the mode this `shale` was built in (alone)\n");
}

// The build's mode, which `shale --version` says in the names Zig's
// `-Doptimize=` gives them, so both builds of Shale say it alike: an optimized
// build is `ReleaseSafe`, since `at` checks every index in every build, and any
// other (TCC's, the sanitizers' -O0) is `Debug`.
static inline Str cli_build_mode(void) {
#ifdef __OPTIMIZE__
    return S("ReleaseSafe");
#else
    return S("Debug");
#endif
}

// The budgets a command line may set, in the order the header's line prints
// them in.
typedef enum {
    SET_STEPS,
    SET_READS,
    SET_READ_BYTES,
    SET_OUTPUT_BYTES,
    SET_CHANGES,
    SET_CHANGE_BYTES,
    SET_ARENA_BYTES,
    SET_CALL_BYTES,
    SET_CALL_ENTRIES,
    SET_KEPT_BYTES,
    SET_MACHINE_BYTES,
    SET_CALL_DEPTH,
    SET_PROGRAMS,
    SET_COUNT,
} Settable;

static inline Str cli_settable_name(size_t k) {
    static const Str names[] = {
        S_ITEM("steps"), S_ITEM("reads"), S_ITEM("read_bytes"), S_ITEM("output_bytes"),
        S_ITEM("changes"), S_ITEM("change_bytes"), S_ITEM("arena_bytes"), S_ITEM("call_bytes"),
        S_ITEM("call_entries"), S_ITEM("kept_bytes"), S_ITEM("machine_bytes"), S_ITEM("call_depth"),
        S_ITEM("programs"),
    };
    _Static_assert(sizeof names / sizeof *names == SET_COUNT, "a name for every settable budget");
    Names n = {names, SET_COUNT};
    return at(n, k);
}

SLICE_TYPE(Flags, bool);

typedef struct {
    Str name;
    Str value;
} Arg;

SLICE_TYPE(ArgSlots, Arg);

enum {
    // The most values the command line takes, and `--args-stdin` as many.
    CLI_ARGS_MOST = 64,
};

typedef struct {
    // `--root`, else SHALE_ROOT, else ".".
    Str root;
    bool has_file;
    Str file;
    bool has_source;
    Str source;
    bool from_stdin;
    bool verbose;
    bool undo_on_stop;
    bool args_stdin;
    bool check;
    Budgets budgets;
    bool flagged[SET_COUNT];
    bool from_header[SET_COUNT];
    Arg args[CLI_ARGS_MOST];
    size_t args_len;
} Command;

// The CLI's default budgets: output 64 KiB and call bytes 64 MiB; the rest
// are the core's.
static inline void cli_defaults(Budgets *b) {
    *b = budgets_default();
    b->output_bytes = 64 * 1024;
    b->call_bytes = 64 * 1024 * 1024;
}

// An integer as a budget's value spells one: an optional `-`, then decimal
// digits, within 64 bits.
static inline bool cli_parse_integer(Str s, int64_t *out) {
    size_t from = s.len > 0 && str_byte(s, 0) == '-' ? 1 : 0;
    if (from == s.len) return false;
    uint64_t v = 0;
    for (size_t i = from; i < s.len; i++) {
        uint8_t c = str_byte(s, i);
        if (c < '0' || c > '9') return false;
        uint64_t d = (uint64_t)(c - '0');
        if (v > (UINT64_MAX - d) / 10) return false;
        v = v * 10 + d;
    }
    if (from == 1) {
        if (v > (uint64_t)INT64_MAX + 1) return false;
        *out = v == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)v;
        return true;
    }
    if (v > (uint64_t)INT64_MAX) return false;
    *out = (int64_t)v;
    return true;
}

static inline int cli_budget_index(Str name) {
    for (size_t k = 0; k < SET_COUNT; k++) {
        if (str_eq(cli_settable_name(k), name)) return (int)k;
    }
    return -1;
}

// The most budget `k` may be set to, or 0 when it has no ceiling.
static inline uint64_t cli_most_of(size_t k) {
    if (k == SET_KEPT_BYTES) return BUDGETS_KEPT_BYTES_MAX;
    if (k == SET_MACHINE_BYTES) return BUDGETS_MACHINE_BYTES_MAX;
    return 0;
}

// `n` as a size_t, SIZE_MAX past it: a plain cast would wrap on a 32-bit size_t
// (the wasm build), where a budget must saturate. A round trip, not
// `n > SIZE_MAX`, which is always false on x86-64 and GCC's -Wtype-limits would
// say so.
static inline size_t cli_size_saturated(uint64_t n) {
    return (uint64_t)(size_t)n == n ? (size_t)n : SIZE_MAX;
}

// Sets budget `k` to `n`, saturating at the field's type.
static inline void cli_set_budget(Budgets *b, size_t k, uint64_t n) {
    uint32_t n32 = n > UINT32_MAX ? UINT32_MAX : (uint32_t)n;
    size_t nz = cli_size_saturated(n);
    switch ((Settable)k) {
    case SET_STEPS: b->steps = n; break;
    case SET_READS: b->reads = n; break;
    case SET_READ_BYTES: b->read_bytes = n; break;
    case SET_OUTPUT_BYTES: b->output_bytes = nz; break;
    case SET_CHANGES: b->changes = n; break;
    case SET_CHANGE_BYTES: b->change_bytes = n; break;
    case SET_ARENA_BYTES: b->arena_bytes = nz; break;
    case SET_CALL_BYTES: b->call_bytes = n; break;
    case SET_CALL_ENTRIES: b->call_entries = n; break;
    case SET_KEPT_BYTES: b->kept_bytes = nz; break;
    case SET_MACHINE_BYTES: b->machine_bytes = nz; break;
    case SET_CALL_DEPTH: b->call_depth = n32; break;
    case SET_PROGRAMS: b->programs = n32; break;
    case SET_COUNT: c_bug("cli: a budget set that is no settable budget");
    }
}

static inline uint64_t cli_budget_value(const Budgets *b, size_t k) {
    switch ((Settable)k) {
    case SET_STEPS: return b->steps;
    case SET_READS: return b->reads;
    case SET_READ_BYTES: return b->read_bytes;
    case SET_OUTPUT_BYTES: return b->output_bytes;
    case SET_CHANGES: return b->changes;
    case SET_CHANGE_BYTES: return b->change_bytes;
    case SET_ARENA_BYTES: return b->arena_bytes;
    case SET_CALL_BYTES: return b->call_bytes;
    case SET_CALL_ENTRIES: return b->call_entries;
    case SET_KEPT_BYTES: return b->kept_bytes;
    case SET_MACHINE_BYTES: return b->machine_bytes;
    case SET_CALL_DEPTH: return b->call_depth;
    case SET_PROGRAMS: return b->programs;
    case SET_COUNT: break;
    }
    c_bug("cli: a budget read that is no settable budget");
    // Never reached: TCC refuses a function that might return no value.
    return 0;
}

static inline const char *cli_find_byte(Str s, char c) {
    return s.len == 0 ? NULL : mem_find_byte(s.ptr, c, s.len);
}

static inline size_t cli_index_of(Str s, char c) {
    const char *p = cli_find_byte(s, c);
    return p == NULL ? SIZE_MAX : (size_t)(p - s.ptr);
}

// A refusal's words, the first `n` of `parts` joined, in `why`; an arena that
// refuses leaves `why` empty.
static inline STATUS_MUST_USE Status cli_refuse_parts(Buf *why, StrPieces parts, size_t n) {
    StrParts all = {parts.piece, n};
    for (size_t k = 0; k < all.len; k++) {
        if (buf_str(why, at(all, k)) != OK) {
            why->len = 0;
            break;
        }
    }
    return STOP;
}

#define cli_refuse(why, n, ...) cli_refuse_parts((why), (StrPieces){{__VA_ARGS__}}, (n))

// A `--budget` or `-- budget:` value, `NAME=N`, checked and set: `who` is how
// a refusal names it (`--budget steps=x` or "`-- budget:` steps=x").
static inline STATUS_MUST_USE Status cli_budget_word(Command *c, Str who, Str v, bool from_header, Buf *why) {
    size_t eq = cli_index_of(v, '=');
    if (eq == SIZE_MAX) return cli_refuse(why, 2, who, S(": it is NAME=N"));
    Str name = str_sub(v, 0, eq);
    Str num = str_sub(v, eq + 1, v.len);
    int64_t n = 0;
    if (!cli_parse_integer(num, &n)) return cli_refuse(why, 4, who, S(": "), num, S(" is not a number"));
    if (n < 1) return cli_refuse(why, 2, who, S(": a budget is at least 1"));
    int k = cli_budget_index(name);
    if (k < 0) return cli_refuse(why, 3, who, S(": no budget is called "), name);
    uint64_t most = cli_most_of((size_t)k);
    if (most > 0 && (uint64_t)n > most) {
        Buf digits = buf_make(why->arena);
        if (buf_u64(&digits, most) != OK) return cli_refuse(why, 0, S(""));
        return cli_refuse(why, 5, who, S(": "), name, S(" is at most "), buf_view(&digits));
    }
    Flags flagged = {c->flagged, SET_COUNT};
    Flags from = {c->from_header, SET_COUNT};
    if (from_header) {
        if (at(flagged, (size_t)k)) return OK;
        at(from, (size_t)k) = true;
    } else {
        at(flagged, (size_t)k) = true;
    }
    cli_set_budget(&c->budgets, (size_t)k, (uint64_t)n);
    return OK;
}

// Parses `argv` without the program's name. STOP with the refusal's words in
// `why`.
static inline STATUS_MUST_USE Status cli_parse(Str *argv, size_t argc, bool has_env_root, Str env_root, Command *c, Buf *why) {
    memset(c, 0, sizeof *c);
    c->root = has_env_root ? env_root : S(".");
    cli_defaults(&c->budgets);
    SLICE_TYPE(Argv, Str);
    Argv all = {argv, argc};
    ArgSlots slots = {c->args, CLI_ARGS_MOST};
    for (size_t i = 0; i < all.len; i++) {
        Str a = at(all, i);
        if (str_eq(a, S("--verbose"))) {
            c->verbose = true;
        } else if (str_eq(a, S("--undo-on-stop"))) {
            c->undo_on_stop = true;
        } else if (str_eq(a, S("--args-stdin"))) {
            c->args_stdin = true;
        } else if (str_eq(a, S("--check"))) {
            c->check = true;
        } else if (str_eq(a, S("--root")) || str_eq(a, S("--budget")) || str_eq(a, S("-e"))) {
            if (i + 1 == all.len) return cli_refuse(why, 2, a, S(" needs a value"));
            i++;
            Str v = at(all, i);
            if (str_eq(a, S("--root"))) {
                c->root = v;
            } else if (str_eq(a, S("-e"))) {
                if (c->has_file || c->has_source) return cli_refuse(why, 1, S("one script: a file or -e, not both"));
                if (c->args_len > 0) return cli_refuse(why, 4, at(slots, 0).name, S("="), at(slots, 0).value, S(" before the script: the script first, then NAME=VALUE"));
                c->has_source = true;
                c->source = v;
            } else {
                Buf who = buf_make(why->arena);
                if (buf_lit(&who, "--budget ") != OK || buf_str(&who, v) != OK) return cli_refuse(why, 0, S(""));
                TRY(cli_budget_word(c, buf_view(&who), v, false, why));
            }
        } else if (str_starts_with(a, S("-"))) {
            return cli_refuse(why, 2, a, S(" is not an option"));
        } else if (cli_index_of(a, '=') != SIZE_MAX) {
            // A value before any script belongs to stdin's script, unless a
            // script follows, which is refused there.
            size_t eq = cli_index_of(a, '=');
            if (eq == 0) return cli_refuse(why, 2, a, S(": a value needs a name"));
            if (c->args_len == CLI_ARGS_MOST) return cli_refuse(why, 1, S("more than 64 values"));
            Arg v = {str_sub(a, 0, eq), str_sub(a, eq + 1, a.len)};
            at(slots, c->args_len) = v;
            c->args_len++;
        } else {
            if (c->has_file || c->has_source) return cli_refuse(why, 3, S("one script: "), a, S(" is a second"));
            if (c->args_len > 0) return cli_refuse(why, 4, at(slots, 0).name, S("="), at(slots, 0).value, S(" before the script: the script first, then NAME=VALUE"));
            c->has_file = true;
            c->file = a;
        }
    }
    c->from_stdin = !c->has_file && !c->has_source;
    if (c->args_stdin && !c->has_file) return cli_refuse(why, 1, S("--args-stdin: stdin is the values of a FILE, and there is no FILE"));
    return OK;
}

static inline Str cli_trim(Str s, Str set) {
    size_t a = 0;
    size_t b = s.len;
    while (a < b && cli_index_of(set, (char)str_byte(s, a)) != SIZE_MAX) a++;
    while (b > a && cli_index_of(set, (char)str_byte(s, b - 1)) != SIZE_MAX) b--;
    return str_sub(s, a, b);
}

// `s` without the spaces it starts with.
static inline Str cli_trim_start(Str s) {
    size_t a = 0;
    while (a < s.len && str_byte(s, a) == ' ') a++;
    return str_sub(s, a, s.len);
}

// The next part of `*rest` up to `sep` (the rest when there is none); false
// once the last part has been given.
static inline bool cli_split(Str *rest, bool *done, char sep, Str *part) {
    if (*done) return false;
    size_t i = cli_index_of(*rest, sep);
    if (i == SIZE_MAX) {
        *part = *rest;
        *done = true;
        return true;
    }
    *part = str_sub(*rest, 0, i);
    *rest = str_sub(*rest, i + 1, rest->len);
    return true;
}

// `--args-stdin`'s values, one `NAME=VALUE` a line, after the command line's,
// into `more` (which holds up to CLI_ARGS_MOST).
static inline STATUS_MUST_USE Status cli_arg_lines(Str text, const Command *c, Arg *more, size_t *more_len, Buf *why) {
    ArgSlots out = {more, CLI_ARGS_MOST};
    size_t n = c->args_len;
    for (size_t i = 0; i < n; i++) {
        ArgSlots have = {(Arg *)c->args, c->args_len};
        at(out, i) = at(have, i);
    }
    Str rest = text;
    bool done = false;
    Str raw = {0};
    uint64_t line_no = 0;
    while (cli_split(&rest, &done, '\n', &raw)) {
        line_no++;
        Str line = raw;
        while (line.len > 0 && str_byte(line, line.len - 1) == '\r') line = str_sub(line, 0, line.len - 1);
        if (cli_trim(line, S(" \t")).len == 0) continue;
        Buf num = buf_make(why->arena);
        if (buf_u64(&num, line_no) != OK) return cli_refuse(why, 0, S(""));
        size_t eq = cli_index_of(line, '=');
        if (eq == SIZE_MAX) return cli_refuse(why, 3, S("--args-stdin: line "), buf_view(&num), S(" is not NAME=VALUE"));
        Str name = str_sub(line, 0, eq);
        if (name.len == 0 || cli_index_of(name, ' ') != SIZE_MAX || cli_index_of(name, '\t') != SIZE_MAX)
            return cli_refuse(why, 5, S("--args-stdin: line "), buf_view(&num), S(": `"), name, S("` is not a name"));
        for (size_t k = 0; k < n; k++) {
            if (str_eq(at(out, k).name, name)) return cli_refuse(why, 5, S("--args-stdin: line "), buf_view(&num), S(": "), name, S(" is given twice"));
        }
        if (n == CLI_ARGS_MOST) return cli_refuse(why, 1, S("more than 64 values"));
        Arg a = {name, str_sub(line, eq + 1, line.len)};
        at(out, n) = a;
        n++;
    }
    *more_len = n;
    return OK;
}

// A line `-- budget: NAME=N ...` among the comments opening a script sets those
// budgets where the command line has not; one after the script's first line of
// code is refused, as it would never be read.
static inline STATUS_MUST_USE Status cli_header_budgets(Str source, Command *c, Buf *why) {
    Str rest = source;
    bool done = false;
    bool seen = false;
    Str raw = {0};
    while (cli_split(&rest, &done, '\n', &raw)) {
        Str line = cli_trim(raw, S(" \t\r"));
        if (line.len == 0) continue;
        if (!str_starts_with(line, S("--"))) {
            Str after = {0};
            while (cli_split(&rest, &done, '\n', &after)) {
                Str l = cli_trim(after, S(" \t\r"));
                if (str_starts_with(l, S("--")) && str_starts_with(cli_trim_start(str_sub(l, 2, l.len)), S("budget:")))
                    return cli_refuse(why, 1, S("a `-- budget:` line after the script's first line of code is never read: put it among the comments opening the script"));
            }
            break;
        }
        Str body = cli_trim_start(str_sub(line, 2, line.len));
        if (!str_starts_with(body, S("budget:"))) continue;
        if (seen) return cli_refuse(why, 1, S("a second `-- budget:` line: one line says them all"));
        seen = true;
        Str words = str_sub(body, 7, body.len);
        bool any = false;
        size_t i = 0;
        while (i < words.len) {
            while (i < words.len && (str_byte(words, i) == ' ' || str_byte(words, i) == '\t')) i++;
            if (i == words.len) break;
            size_t j = i;
            while (j < words.len && str_byte(words, j) != ' ' && str_byte(words, j) != '\t') j++;
            Str v = str_sub(words, i, j);
            i = j;
            any = true;
            Buf who = buf_make(why->arena);
            if (buf_lit(&who, "`-- budget:` ") != OK || buf_str(&who, v) != OK) return cli_refuse(why, 0, S(""));
            TRY(cli_budget_word(c, buf_view(&who), v, true, why));
        }
        if (!any) return cli_refuse(why, 1, S("`-- budget:` names no budget"));
    }
    return OK;
}

// One line naming what the header's budgets set, or nothing.
static inline STATUS_MUST_USE Status cli_write_header_budgets(Buf *w, const Command *c) {
    bool first = true;
    Flags from = {(bool *)c->from_header, SET_COUNT};
    for (size_t k = 0; k < SET_COUNT; k++) {
        if (!at(from, k)) continue;
        TRY(buf_str(w, first ? S("── budgets from the script's header: ") : S(", ")));
        TRY(buf_str(w, cli_settable_name(k)));
        TRY(buf_byte(w, '='));
        TRY(buf_u64(w, cli_budget_value(&c->budgets, k)));
        first = false;
    }
    if (!first) TRY(buf_byte(w, '\n'));
    return OK;
}

// The script's name in a stop: its file, `-e` or `stdin`.
static inline Str cli_script_name(const Command *c) {
    if (c->has_file) return c->file;
    return c->has_source ? S("-e") : S("stdin");
}

// What a check used, as the report's `── used:` line says it.
typedef struct {
    uint64_t steps;
    uint64_t reads;
    uint64_t read_bytes;
} Used;

// The report for a check that stopped: the stop's line (a parse's stop is at
// the top level, so it has no function and no trail), then what it used.
static inline STATUS_MUST_USE Status cli_write_stop(Buf *w, Str name, const Stop *s, Used u, const Budgets *b) {
    stop_check_budget(s);
    TRY(stop_report(w, name, s, u.steps));
    TRY(buf_lit(w, "── used: "));
    TRY(buf_u64(w, u.steps));
    TRY(buf_lit(w, " of "));
    TRY(buf_u64(w, b->steps));
    TRY(buf_lit(w, " steps, "));
    TRY(buf_u64(w, u.reads));
    TRY(buf_lit(w, " of "));
    TRY(buf_u64(w, b->reads));
    TRY(buf_lit(w, " reads, "));
    TRY(buf_u64(w, u.read_bytes));
    TRY(buf_lit(w, " of "));
    TRY(buf_u64(w, b->read_bytes));
    TRY(buf_lit(w, " read bytes, 0 of "));
    TRY(buf_u64(w, b->output_bytes));
    return buf_lit(w, " output bytes\n");
}

// The source a stop's `function` is in: the core's prelude, a later prelude or
// a `use`d library (whose functions are shown as `<its name>'s f`), or the
// script, `name`.
static inline Str cli_place_of(Str function, Str name, Preludes preludes) {
    if (str_starts_with(function, S("the prelude's "))) return S("the prelude");
    for (size_t k = 0; k < preludes.len; k++) {
        Str pn = at(preludes, k).name;
        if (function.len > pn.len + 3 && str_starts_with(function, pn) && str_starts_with(str_sub(function, pn.len, function.len), S("'s "))) return pn;
    }
    for (size_t i = 0; i + 3 <= function.len; i++) {
        if (str_eq(str_sub(function, i, i + 3), S("'s "))) return str_sub(function, 0, i);
    }
    return name;
}

static inline STATUS_MUST_USE Status cli_write_where(Buf *w, Str place, uint32_t line, uint32_t column, Str function) {
    return stop_write_where(w, place, line, column, function);
}

// What a run used, the report's last line counts.
typedef struct {
    uint64_t steps;
    uint64_t reads;
    uint64_t read_bytes;
    uint64_t output;
} RunUsed;

// The report for a run: with `verbose`, its effects, each line
// `op path: outcome`; a stop's line and its calls, innermost first, each named
// by its source; `── finished` when verbose; and the `── used:` line
// when verbose, stopped, or past half of a budget.
static inline STATUS_MUST_USE Status cli_write_report(Buf *w, Str name, Preludes preludes, bool verbose, Names effects, Names records, const Stop *s, RunUsed u, const Budgets *b) {
    if (verbose) {
        TRY(buf_lit(w, "── effects ("));
        TRY(buf_u64(w, effects.len));
        TRY(buf_lit(w, ") ──\n"));
        for (size_t k = 0; k < effects.len; k++) {
            TRY(buf_str(w, at(effects, k)));
            TRY(buf_byte(w, '\n'));
        }
    }
    if (verbose && records.len > 0) {
        TRY(buf_lit(w, "── ran ──\n"));
        for (size_t k = 0; k < records.len; k++) {
            TRY(buf_str(w, at(records, k)));
            TRY(buf_byte(w, '\n'));
        }
    }
    if (s != NULL) {
        TRY(buf_lit(w, "── stopped at "));
        TRY(cli_write_where(w, cli_place_of(s->function, name, preludes), s->line, s->column, s->function));
        TRY(buf_lit(w, ": "));
        TRY(buf_str(w, stop_class_name(s->cls)));
        if (s->budget != BUDGET_NONE) {
            TRY(buf_byte(w, ' '));
            TRY(buf_str(w, budget_name(s->budget)));
        }
        TRY(buf_lit(w, ": "));
        TRY(buf_str(w, s->message));
        TRY(buf_lit(w, " ("));
        TRY(buf_u64(w, u.steps));
        TRY(buf_lit(w, " steps)\n"));
        for (size_t k = s->trail.len; k > 0; k--) {
            const Call c = at(s->trail, k - 1);
            TRY(buf_lit(w, "   called from "));
            TRY(cli_write_where(w, cli_place_of(c.function, name, preludes), c.line, c.column, c.function));
            TRY(buf_byte(w, '\n'));
        }
    } else if (verbose) {
        TRY(buf_lit(w, "── finished ("));
        TRY(buf_u64(w, u.steps));
        TRY(buf_lit(w, " steps)\n"));
    }
    // Past half a budget: twice what was used is past it, in 64 bits.
    bool past_half = u.steps * 2 > b->steps || u.reads * 2 > b->reads || u.read_bytes * 2 > b->read_bytes || u.output * 2 > b->output_bytes;
    if (!(verbose || s != NULL || past_half)) return OK;
    TRY(buf_lit(w, "── used: "));
    TRY(buf_u64(w, u.steps));
    TRY(buf_lit(w, " of "));
    TRY(buf_u64(w, b->steps));
    TRY(buf_lit(w, " steps, "));
    TRY(buf_u64(w, u.reads));
    TRY(buf_lit(w, " of "));
    TRY(buf_u64(w, b->reads));
    TRY(buf_lit(w, " reads, "));
    TRY(buf_u64(w, u.read_bytes));
    TRY(buf_lit(w, " of "));
    TRY(buf_u64(w, b->read_bytes));
    TRY(buf_lit(w, " read bytes, "));
    TRY(buf_u64(w, u.output));
    TRY(buf_lit(w, " of "));
    TRY(buf_u64(w, b->output_bytes));
    return buf_lit(w, " output bytes\n");
}

// The first value given that no `arg("NAME")` of the program reads, as a stop
// at 1:1 naming what it reads. `*found` is false when each is read, or the
// program names one by a value.
static inline STATUS_MUST_USE Status cli_unread_arg(Arena *a, const Program *prog, const Arg *args, size_t n, Stop *out, bool *found) {
    *found = false;
    if (prog->args_by_value) return OK;
    SLICE_TYPE(Args, const Arg);
    Args given = {args, n};
    for (size_t i = 0; i < given.len; i++) {
        Str name = at(given, i).name;
        bool read = false;
        for (size_t k = 0; k < prog->args_read.len; k++) {
            if (str_eq(at(prog->args_read, k), name)) read = true;
        }
        if (read) continue;
        Msg m;
        msg_init(&m, a);
        msg_str(&m, S("`"));
        msg_str(&m, name);
        msg_str(&m, S("=` is given, and the script reads no `arg(\""));
        msg_str(&m, name);
        msg_str(&m, S("\")`: "));
        if (prog->args_read.len == 0) {
            msg_str(&m, S("it reads none"));
        } else {
            msg_str(&m, S("it reads "));
            for (size_t k = 0; k < prog->args_read.len; k++) {
                if (k > 0) msg_str(&m, S(", "));
                msg_str(&m, at(prog->args_read, k));
            }
        }
        if (m.s != OK) return STOP;
        Stop s = {0};
        memset(&s, 0, sizeof s);
        s.line = 1;
        s.column = 1;
        s.cls = CLASS_ARGUMENT;
        s.message = buf_view(&m.b);
        s.function = S("");
        *out = s;
        *found = true;
        return OK;
    }
    return OK;
}

// The longest name FAT holds on the bare-metal host, which must agree with
// zig-webbox's store (`max_name`).
enum { CLI_NAME_MOST = 96 };

// Whether `name` is a part of a path FAT can hold.
static inline bool cli_fat_name(Str name) {
    if (name.len == 0 || name.len > CLI_NAME_MOST) return false;
    if (str_eq(name, S(".")) || str_eq(name, S(".."))) return false;
    for (size_t i = 0; i < name.len; i++) {
        uint8_t c = str_byte(name, i);
        if (c < 0x20 || c > 0x7e) return false;
        if (cli_index_of(S("\"*/:<>?\\|"), (char)c) != SIZE_MAX) return false;
    }
    uint8_t last = str_byte(name, name.len - 1);
    return last != '.' && last != ' ';
}

#endif
