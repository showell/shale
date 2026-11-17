// regex.h: the linear-time regular expressions behind `re_find`, `re_match`,
// `re_replace`, `re_find_all` and the host's `re_lines`: Zig Shale's
// src/script/regex.zig, the same subset, answers, refusals and counts.
//
// A pattern compiles to a Pike VM program. A search advances every thread a
// byte at a time in order of preference and follows each state at most once a
// position, so it finds the match a backtracker would find first, in at most
// (bytes + 1) times the program's states, whatever the pattern. The counts a
// search gives (`positions`, `skipped`, `work`) are what the builtins charge
// steps by, so they are Zig's exactly. Shale has its own engine so that every
// search is linear in its input: PCRE backtracks, and RE2 is C++.
//
// The functions are in regex.c, which a program includes after this header:
// the C is one translation unit, every function `static`.

#ifndef SHALE_REGEX_H
#define SHALE_REGEX_H

#include "arena.h"
#include "status.h"

enum {
    // The longest pattern; a longer one is refused before it is read.
    REGEX_PATTERN_MOST = 256,
    // The most capture groups: `\1` to `\9` in a replacement.
    REGEX_GROUPS_MOST = 9,
    // The largest count a `{n,m}` may give.
    REGEX_COUNT_MOST = 100,
    // The most instructions a program may have, its counts written out, so a
    // place fits a uint16_t.
    REGEX_INSTS_MOST = 8192,
    // Loops whose turn can take nothing: at most 16 a pattern, nested at most
    // 4 deep, so a position holds at most 16 states an instruction.
    REGEX_EMPTY_LOOPS_MOST = 16,
    REGEX_NEST_MOST = 4,
    // A thread's captures: the match's start, then each group's start and end.
    REGEX_CAPS = 1 + 2 * REGEX_GROUPS_MOST,
    // The compiled patterns a run keeps, the least recently used evicted.
    REGEX_CACHE_MOST = 8,
};

typedef enum {
    // The byte `c`.
    REGEX_OP_BYTE,
    // Any byte but a newline.
    REGEX_OP_ANY,
    // A byte in set `x`.
    REGEX_OP_SET,
    // Continue at `x`, and, less preferred, at `y`.
    REGEX_OP_SPLIT,
    REGEX_OP_JMP,
    // Record the position in capture slot `x`.
    REGEX_OP_SAVE,
    REGEX_OP_START,
    REGEX_OP_END,
    // A word edge (`c` 0) or not one (`c` 1).
    REGEX_OP_BOUNDARY,
    // A turn of loop `x` begins: its "took something" bit is cleared.
    REGEX_OP_TURN,
    // A turn of loop `x` ends: one that took nothing ends the loop here.
    REGEX_OP_AGAIN,
    // A `+`'s first turn ends: one that took nothing goes on at `y`.
    REGEX_OP_FIRST,
    REGEX_OP_MATCH,
} RegexOp;

// Six bytes, as Zig's: `op` is a RegexOp held in a uint8_t, Zig's `enum(u8)`.
// With a 4-byte enum `op` it would be 12, a size TCC 0.9.27 miscompiles when
// a function returns it straight into a call (style.md, "Declarations").
// `regex_emit` takes the fields.
typedef struct {
    uint8_t op;
    uint8_t c;
    uint16_t x;
    uint16_t y;
} RegexInst;

_Static_assert(sizeof(RegexInst) == 6, "an instruction is Zig's six bytes");

// 256 bits, one a byte.
typedef struct {
    uint64_t words[4];
} RegexSet;

// The loops whose turn can take nothing around one instruction, by number.
typedef struct {
    uint8_t n;
    uint8_t ids[REGEX_NEST_MOST];
} RegexEncl;

SLICE_TYPE(RegexInsts, RegexInst);
SLICE_TYPE(RegexSets, RegexSet);
SLICE_TYPE(RegexEncls, RegexEncl);
SLICE_TYPE(RegexBases, uint32_t);

typedef struct {
    RegexInsts insts;
    RegexSets sets;
    uint8_t groups;
    // Whether every alternative begins with `^`.
    bool anchored;
    RegexEncls encl;
    // Where each instruction's states start, one for each setting of its
    // `encl` bits.
    RegexBases base;
    // The states a position can hold: what a search is charged per position.
    uint32_t states;
    // The bytes a match can begin with, when every match takes one and some
    // byte cannot begin one. While no thread is alive, a search skips the rest.
    bool has_first;
    RegexSet first;
} RegexProgram;

typedef struct {
    // ASCII letters match either case, folded at compile time; a class is
    // folded before its `^` is applied, as in Python.
    bool nocase;
} RegexOptions;

// Why a pattern was refused: `refused` set, the byte offset, and Zig's text.
typedef struct {
    bool refused;
    size_t at;
    Str message;
} RegexRefusal;

typedef struct {
    uint16_t pc;
    size_t caps[REGEX_CAPS];
    // Bit k set when loop k's turn has taken a byte.
    uint16_t flags;
} RegexThread;

SLICE_TYPE(RegexThreads, RegexThread);
SLICE_TYPE(RegexStamps, uint64_t);

// The threads at one position, in order of preference. `stamp[st] == now`
// marks a state already followed here; a reset advances `now`.
typedef struct {
    RegexThreads threads;
    RegexStamps stamp;
    uint64_t now;
    size_t len;
} RegexList;

// A program's working memory, made once and reused by every search.
typedef struct {
    RegexList a;
    RegexList b;
    // A split's second way, waiting while its first is followed.
    RegexThreads stack;
} RegexScratch;

// A group the match passed through, `[start, end)` of the string.
typedef struct {
    bool matched;
    size_t start;
    size_t end;
} RegexGroup;

typedef struct {
    size_t start;
    size_t end;
    // Group 1 first; those past the program's `groups` are unmatched.
    RegexGroup groups[REGEX_GROUPS_MOST];
} RegexFound;

typedef struct {
    bool found;
    RegexFound match;
    // Positions examined; a search costs this times the program's states.
    uint64_t positions;
    // Bytes passed over while no thread was alive, each looked at once.
    uint64_t skipped;
    // Instructions followed (the run's `work` tally).
    uint64_t work;
    // False when a count in RegexMost stopped the search early.
    bool finished;
} RegexSearched;

// How far a search may go before it stops unfinished.
typedef struct {
    uint64_t positions;
    uint64_t skipped;
} RegexMost;

typedef struct {
    bool present;
    // The entry's own arena: its program, its scratch and its text, all given
    // back at once when it is evicted, as Zig's machine heap gives them back.
    Arena arena;
    Str text;
    bool nocase;
    RegexProgram program;
    RegexScratch scratch;
    uint64_t used;
} RegexCached;

SLICE_TYPE(RegexEntries, RegexCached);

typedef struct {
    RegexCached entries[REGEX_CACHE_MOST];
    uint64_t clock;
} RegexCache;

#endif
