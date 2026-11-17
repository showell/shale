// path_run.c: a script's path as the core takes it, the port of Zig
// Shale's `builtins_store.pathArg` and `absoluteStop` (src/script/
// builtins_store.zig) and of the hook a host hangs on every path,
// `interp.PathHook`. Also where every stop built from a path is made
// (`path_fail`), which holds the secrets rule's one check, and the store's
// failure in words (`path_store_failed`).
//
// A path is the root's unless the hook resolves it (a mount's, on Linux):
// every part a name FAT holds, so `.`, `..`, empty parts and a leading `/`
// are refused, `""` the root for the calls that only look. Metal has no
// hook, and its paths are the root's.
//
// Include store.c first.

#include "err.h"


// Declared ahead, for the hook's calls; defined below the hook.
typedef struct PathRun PathRun;

// An absolute path spelled as a script's (`PathHook.absolute_hint`):
// `has_mount` false for the root, and `spelled` the path to write.
typedef struct {
    bool has_mount;
    Str mount;
    Str spelled;
} AbsoluteHint;

// The one hook on paths, `interp.PathHook`, as a vtable. `resolve` gives
// the OS path a mount's path names (`*resolved` false for the root's
// rules); `may_change` is asked before anything is written and refuses a
// read-only mount's path; `absolute_hint` gives the folder that holds an
// absolute path, `*has` false when none may be named; `names_secret` says
// whether a text names a secrets mount's folder.
typedef struct {
    void *ctx;
    Status (*resolve)(void *ctx, PathRun *r, Pos pos, Str path, bool *resolved, Str *full);
    Status (*may_change)(void *ctx, PathRun *r, Pos pos, Str rel, Str full);
    Status (*absolute_hint)(void *ctx, PathRun *r, Pos pos, Str abs, bool *has, AbsoluteHint *out);
    Status (*names_secret)(void *ctx, PathRun *r, Pos pos, Str text, bool *names);
} PathHook;

// What a path's check needs of the run: where its stops go, the run's
// step charge, the data root, and the host's hook (NULL on metal).
typedef struct PathRun {
    RunCtx *ctx;
    // The data root as the run was given it, Zig's `run.root`.
    Str root;
    // The run's step charge, Zig's `Run.charge`: STOP past the budget.
    Status (*charge)(void *vm, RunCtx *ctx, Pos pos, uint64_t n);
    void *vm;
    const PathHook *hook;
} PathRun;

static STATUS_MUST_USE Status path_charge(PathRun *r, Pos pos, uint64_t n) {
    return r->charge(r->vm, r->ctx, pos, n);
}

// What an argument of a stop's message is: the script's own text, which a
// message may give back, or the host's, which the secrets check reads.
typedef enum {
    MSG_SCRIPT,
    MSG_HOST,
    // The end of MSG_ARGS's list, never a message's.
    MSG_END,
} MsgKind;

typedef struct {
    Str text;
    MsgKind kind;
} MsgArg;

SLICE_TYPE(MsgArgs, const MsgArg);

// An argument of MSG_ARGS, as a brace initializer: TCC 0.9.27 refuses a
// compound literal inside the array's initializer ("index too large").
#define MSG_FROM_SCRIPT(s) {(s), MSG_SCRIPT}
#define MSG_FROM_HOST(s) {(s), MSG_HOST}
// The list ends with an MSG_END item and is counted when it is made: a
// brace item's comma would split a preprocessor count, and TCC 0.9.27
// refuses `sizeof` of the compound literal when an item calls a function.
typedef const MsgArg MsgArgArray[];
#define MSG_ARGS(...) msg_args_of((MsgArgArray){__VA_ARGS__, {{NULL, 0}, MSG_END}})

// DRAGON: bounds. The walk to MSG_END is unchecked, so a list without its
// end would be read past.
// REMEDY: only MSG_ARGS calls this, and it appends the end to every list it
// makes.
static MsgArgs msg_args_of(const MsgArg *items) {
    MsgArgs open = {items, SIZE_MAX};
    size_t n = 0;
    while (at_unchecked(open, n, "MSG_ARGS ends every list with MSG_END").kind != MSG_END) n++;
    MsgArgs a = {items, n};
    return a;
}

// Room for a number's digits, so a message's number needs no arena.
typedef struct {
    char digits[20];
} MsgNum;

// `n` in decimal, as Zig's `{d}`, its digits in `room`.
static Str msg_num(MsgNum *room, uint64_t n) {
    Bytes d = {room->digits, sizeof room->digits};
    size_t k = d.len;
    do {
        k--;
        at(d, k) = (char)('0' + n % 10);
        n /= 10;
    } while (n > 0);
    return str_sub(((Str){room->digits, sizeof room->digits}), k, sizeof room->digits);
}

// `fmt`, Zig's format string, with each `{s}`, `{d}` or `{t}` the next of
// `args` and `{{` and `}}` one brace. A placeholder with no argument, or an
// argument left over, is a bug in the C.
static STATUS_MUST_USE Status msg_format(Buf *b, Str fmt, MsgArgs args) {
    size_t k = 0;
    for (size_t i = 0; i < fmt.len; i++) {
        char c = at(fmt, i);
        bool next_same = i + 1 < fmt.len && at(fmt, i + 1) == c;
        if ((c == '{' || c == '}') && next_same) {
            TRY(buf_byte(b, c));
            i++;
            continue;
        }
        if (c == '{') {
            while (i < fmt.len && at(fmt, i) != '}') i++;
            if (k >= args.len) {
                fprintf(stderr, "path_run: a message's placeholder has no argument\n");
                abort();
            }
            TRY(buf_str(b, at(args, k).text));
            k++;
            continue;
        }
        TRY(buf_byte(b, c));
    }
    if (k != args.len) {
        fprintf(stderr, "path_run: a message has an argument left over\n");
        abort();
    }
    return OK;
}

// Stops the run at `pos` with Zig's message `fmt` of `args`, or `fmt` itself
// when the arena refuses it, as Zig's `Run.fail` falls back to its format
// string. Every stop built from a path, in path_run.c and paths.c, is made
// here.
// DRAGON: the secrets rule. A host-made argument (an OS path, a mount's
// folder) that names a secrets mount's folder would print where the tokens
// are.
// REMEDY: the rule's one check, here: such an argument ends the process, a
// bug in the C, before any message is made. A script's own text may come
// back (the plain absolute stop gives back what the script wrote); no host
// text may, a mount's folder included (paths.c's `paths_mount_path` names a
// secrets mount alone).
// test_paths plants a host argument that names one.
static STATUS_MUST_USE Status path_fail(PathRun *r, Pos pos, StopClass cls, Str fmt, MsgArgs args) {
    if (r->hook != NULL && r->hook->names_secret != NULL) {
        for (size_t k = 0; k < args.len; k++) {
            if (at(args, k).kind != MSG_HOST) continue;
            bool names = false;
            TRY(r->hook->names_secret(r->hook->ctx, r, pos, at(args, k).text, &names));
            if (names) {
                fprintf(stderr, "path_run: a stop's message names a secrets mount's folder\n");
                abort();
            }
        }
    }
    Buf b = buf_make(r->ctx->values);
    Status built = msg_format(&b, fmt, args);
    return run_fail_built(r->ctx, pos, cls, &b, built, fmt);
}

// A store error in plain words, Zig's `storeWords`; its name follows in
// parentheses.
static Str path_store_words(StoreErr e) {
    switch (e) {
    case STORE_FILE_NOT_FOUND: return S("not found");
    case STORE_NOT_DIR: return S("a part of the path is a file, not a folder");
    case STORE_IS_DIR: return S("a folder, not a file");
    case STORE_NOT_FILE: return S("not a file");
    case STORE_PATH_ALREADY_EXISTS: return S("already there");
    case STORE_DIR_NOT_EMPTY: return S("the folder is not empty");
    case STORE_ACCESS_DENIED: return S("the store refuses it");
    case STORE_PERMISSION_DENIED: return S("the store refuses it");
    case STORE_NO_SPACE_LEFT: return S("the store is full");
    case STORE_SYM_LINK_LOOP: return S("a link loop");
    case STORE_NAME_TOO_LONG: return S("the name is too long");
    default: return S("the store failed");
    }
}

// What to do about it, when there is something: Zig's `storeHint`.
static Str path_store_hint(StoreErr e) {
    switch (e) {
    case STORE_FILE_NOT_FOUND: return S(": `exists(p)` asks first");
    case STORE_NOT_DIR: return S(": `is_dir(p)` asks first");
    case STORE_IS_DIR: return S(": `is_dir(p)` asks first");
    case STORE_NOT_FILE: return S(": `is_dir(p)` asks first");
    case STORE_PATH_ALREADY_EXISTS: return S(": `exists(p)` asks first");
    case STORE_DIR_NOT_EMPTY: return S(": `remove_tree(p)` removes a folder and what is in it");
    default: return S("");
    }
}

// The stop for a store call `who` made on the script's `rel` that failed
// with `e`: `who("rel")`: words (Name)hint, as Zig's `storeFailed`; the
// arena budget for OutOfMemory.
static STATUS_MUST_USE Status path_store_failed(PathRun *r, Pos pos, Str who, Str rel, StoreErr e) {
    if (e == STORE_OUT_OF_MEMORY) return run_oom(r->ctx, pos);
    return path_fail(r, pos, CLASS_STORE, S("`{s}(\"{s}\")`: {s} ({s}){s}"),
                     MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(rel), MSG_FROM_HOST(path_store_words(e)), MSG_FROM_HOST(store_err_name(e)), MSG_FROM_HOST(path_store_hint(e))));
}

// Where the part of `s` that starts at `from` ends: the next `sep` (`/` for
// a path's part), or the end. Walking `from = end + 1` until `end == s.len`
// visits every part of Zig's `splitScalar(s, sep)`, an empty one included.
static size_t path_part_end_at(Str s, size_t from, char sep) {
    size_t i = from;
    while (i < s.len && at(s, i) != sep) i++;
    return i;
}

static size_t path_part_end(Str s, size_t from) {
    return path_part_end_at(s, from, '/');
}

// A script's path, confined: `full` is the OS path (the root's, or the
// hook's), `rel` the script's own. Only `path_arg` makes one, so a call
// that takes a StorePath took a checked one (Zig's sealed `Path`).
typedef struct {
    Str full;
    Str rel;
} StorePath;

// The stop for an absolute path, the one home of its words: where the hook
// knows the folder that holds it, the stop says how to write it there.
// Zig's `absoluteStop`.
static STATUS_MUST_USE Status path_absolute_stop(PathRun *r, Pos pos, Str who, Str abs) {
    if (r->hook == NULL || r->hook->absolute_hint == NULL) {
        return path_fail(r, pos, CLASS_ARGUMENT, S("`{s}`: \"{s}\" is absolute: paths are relative to the data root"), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(abs)));
    }
    bool has = false;
    AbsoluteHint hint;
    memset(&hint, 0, sizeof hint);
    TRY(r->hook->absolute_hint(r->hook->ctx, r, pos, abs, &has, &hint));
    if (!has) {
        return path_fail(r, pos, CLASS_ARGUMENT, S("`{s}`: \"{s}\" is absolute: paths are relative to the data root, or a mount's, `name:path` (`shale --help` lists this box's mounts)"),
                         MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(abs)));
    }
    if (hint.has_mount) {
        return path_fail(r, pos, CLASS_ARGUMENT, S("`{s}`: \"{s}\" is absolute: it's under the mount `{s}:`, so write \"{s}\""),
                         MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(abs), MSG_FROM_HOST(hint.mount), MSG_FROM_HOST(hint.spelled)));
    }
    return path_fail(r, pos, CLASS_ARGUMENT, S("`{s}`: \"{s}\" is absolute: it's under the data root, so write \"{s}\""), MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(abs), MSG_FROM_HOST(hint.spelled)));
}

// `given`, the path a call `who` was given, confined (Zig's `pathArg`, its
// string already checked by the caller): `.` is the root and `m:.` the
// mount `m`; the hook resolves a mount's path; `""` is the root only when
// `allow_root`; a leading `/`, a `..` and a part FAT refuses each stop. The
// steps charged are the path's bytes.
// DRAGON: the store's boundaries. Every path a builtin hands the store
// comes through here, so a path that skipped it could name anything the
// process can reach; the hook's answer is trusted whole.
// REMEDY: `..`, a leading `/`, an empty part and the whole root are refused
// below, and paths.c checks a mount's every part itself; test_paths plants
// each.
static STATUS_MUST_USE Status path_arg(PathRun *r, Pos pos, Str who, Str given, bool allow_root, StorePath *out) {
    Str rel = given;
    if (str_eq(given, S("."))) {
        rel = S("");
    } else if (given.len >= 2 && str_eq(str_sub(given, given.len - 2, given.len), S(":.")) && memchr(given.ptr, '/', given.len) == NULL) {
        rel = str_sub(given, 0, given.len - 1);
    }
    TRY(path_charge(r, pos, given.len));
    if (r->hook != NULL) {
        bool resolved = false;
        Str full = S("");
        TRY(r->hook->resolve(r->hook->ctx, r, pos, rel, &resolved, &full));
        if (resolved) {
            out->full = full;
            out->rel = rel;
            return OK;
        }
    }
    if (rel.len == 0) {
        if (!allow_root) return path_fail(r, pos, CLASS_ARGUMENT, S("`{s}(\"\")` would act on the whole root: name a path of at least one part"), MSG_ARGS(MSG_FROM_HOST(who)));
        out->full = r->root;
        out->rel = S("");
        return OK;
    }
    if (str_byte(rel, 0) == '/') return path_absolute_stop(r, pos, who, rel);
    size_t from = 0;
    while (from <= rel.len) {
        size_t to = path_part_end(rel, from);
        Str part = str_sub(rel, from, to);
        if (str_eq(part, S(".."))) {
            return path_fail(r, pos, CLASS_ARGUMENT, S("`{s}`: \"{s}\" climbs out of the root with `..`: a folder beside it is reached through a mount, such as `repos:<name>/...` (`shale --help` lists this box's mounts)"),
                             MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(rel)));
        }
        if (!store_fat_name(part)) {
            return path_fail(r, pos, CLASS_ARGUMENT, S("`{s}`: \"{s}\" has a part (\"{s}\") that is not a name: no `.`, `..`, empty parts or characters FAT refuses"),
                             MSG_ARGS(MSG_FROM_HOST(who), MSG_FROM_SCRIPT(rel), MSG_FROM_SCRIPT(part)));
        }
        if (to == rel.len) break;
        from = to + 1;
    }
    // A root of `/` takes no second `/`: `//x` is an empty part to the store.
    bool sep = r->root.len == 0 || str_byte(r->root, r->root.len - 1) != '/';
    Buf b = buf_make(r->ctx->values);
    if (buf_str(&b, r->root) != OK || (sep && buf_byte(&b, '/') != OK) || buf_str(&b, rel) != OK) return run_oom(r->ctx, pos);
    out->full = buf_view(&b);
    out->rel = rel;
    return OK;
}
