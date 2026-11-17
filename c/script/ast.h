// ast.h: the tree a script parses to, Zig Shale's src/script/ast.zig. Every
// node has its line and column, every name is already resolved to a slot in
// its function's frame, and every call names a builtin or a function by index.
// All of it lives in the machine's arena.
//
// The layouts are the C's own: arena byte counts are outside the frozen
// surface, so nothing here mirrors Zig's sizes.

#ifndef SHALE_AST_H
#define SHALE_AST_H

#include "../foundation/err.h"

// The builtins, in ast.zig's order; `ast_builtin_names` spells each.
typedef enum {
    BUILTIN_LS,
    BUILTIN_STAT,
    BUILTIN_EXISTS,
    BUILTIN_IS_DIR,
    BUILTIN_READ,
    BUILTIN_READ_OR,
    BUILTIN_READ_LINES,
    BUILTIN_LINE_AT,
    BUILTIN_GREP,
    BUILTIN_COUNT_LINES,
    BUILTIN_COUNT_IN,
    BUILTIN_FIND_IN,
    BUILTIN_REPLACE,
    BUILTIN_APPEND,
    BUILTIN_MKDIR,
    BUILTIN_REMOVE,
    BUILTIN_REMOVE_TREE,
    BUILTIN_WALK,
    BUILTIN_EDIT,
    BUILTIN_SPLIT,
    BUILTIN_JOIN,
    BUILTIN_STARTS_WITH,
    BUILTIN_ENDS_WITH,
    BUILTIN_CONTAINS,
    BUILTIN_FIND,
    BUILTIN_SUB,
    BUILTIN_BAD_UTF8,
    BUILTIN_TRIM,
    BUILTIN_TRIM_START,
    BUILTIN_TRIM_END,
    BUILTIN_UPPER,
    BUILTIN_LOWER,
    BUILTIN_TOSTRING,
    BUILTIN_TONUMBER,
    BUILTIN_PARSE_INT,
    BUILTIN_LINES,
    BUILTIN_WORDS,
    BUILTIN_LIKE,
    BUILTIN_REP,
    BUILTIN_SUBSTITUTE,
    BUILTIN_PATH,
    BUILTIN_MATCH_PATH,
    BUILTIN_BASENAME,
    BUILTIN_IS_NAME,
    BUILTIN_STRIP_SUFFIX,
    BUILTIN_BETWEEN,
    BUILTIN_RE_FIND,
    BUILTIN_RE_MATCH,
    BUILTIN_RE_REPLACE,
    BUILTIN_RE_FIND_ALL,
    BUILTIN_MIN,
    BUILTIN_MAX,
    BUILTIN_ABS,
    BUILTIN_DATE,
    BUILTIN_SECONDS,
    BUILTIN_HUMAN,
    BUILTIN_PAD,
    BUILTIN_TABLE,
    BUILTIN_PUSH,
    BUILTIN_POP,
    BUILTIN_SORT,
    BUILTIN_KEYS,
    BUILTIN_HAS_KEY,
    BUILTIN_GET,
    BUILTIN_COUNT,
    BUILTIN_REVERSE,
    BUILTIN_TAKE,
    BUILTIN_LAST,
    BUILTIN_SAME,
    BUILTIN_NOW,
    BUILTIN_ARG,
    BUILTIN_FAIL,
    BUILTIN_HOST,
    BUILTIN_BUDGET,
    BUILTIN_CALL_BYTES,
    BUILTIN_YIELD,
    BUILTIN_PRINT,
    BUILTIN_COUNT_OF,
} Builtin;

SLICE_TYPE(BuiltinNames, const Str);

static inline BuiltinNames ast_builtin_names(void) {
    static const Str names[BUILTIN_COUNT_OF] = {
        S("ls"), S("stat"), S("exists"), S("is_dir"), S("read"), S("read_or"), S("read_lines"),
        S("line_at"), S("grep"), S("count_lines"), S("count_in"), S("find_in"), S("replace"),
        S("append"), S("mkdir"), S("remove"), S("remove_tree"), S("walk"), S("edit"), S("split"),
        S("join"), S("starts_with"), S("ends_with"), S("contains"), S("find"), S("sub"),
        S("bad_utf8"), S("trim"), S("trim_start"), S("trim_end"), S("upper"), S("lower"),
        S("tostring"), S("tonumber"), S("parse_int"), S("lines"), S("words"), S("like"), S("rep"),
        S("substitute"), S("path"), S("match_path"), S("basename"), S("is_name"),
        S("strip_suffix"), S("between"), S("re_find"), S("re_match"), S("re_replace"),
        S("re_find_all"), S("min"), S("max"), S("abs"), S("date"), S("seconds"), S("human"),
        S("pad"), S("table"), S("push"), S("pop"), S("sort"), S("keys"), S("has_key"), S("get"),
        S("count"), S("reverse"), S("take"), S("last"), S("same"), S("now"), S("arg"), S("fail"),
        S("host"), S("budget"), S("call_bytes"), S("yield"), S("print"),
    };
    BuiltinNames n = {names, BUILTIN_COUNT_OF};
    return n;
}

// The builtin named `name`, as Zig's `std.meta.stringToEnum`; false when none is.
static inline bool ast_builtin_named(Str name, Builtin *out) {
    BuiltinNames n = ast_builtin_names();
    for (size_t i = 0; i < n.len; i++) {
        if (str_eq(at(n, i), name)) {
            *out = (Builtin)i;
            return true;
        }
    }
    return false;
}

// `BINOP_EQ_NIL` and `BINOP_NE_NIL` are `==` and `~=` with the literal `nil`
// on one side: the one comparison a map or a list may be in.
typedef enum {
    BINOP_ADD,
    BINOP_SUB,
    BINOP_MUL,
    BINOP_IDIV,
    BINOP_MOD,
    BINOP_CONCAT,
    BINOP_EQ,
    BINOP_NE,
    BINOP_EQ_NIL,
    BINOP_NE_NIL,
    BINOP_LT,
    BINOP_LE,
    BINOP_GT,
    BINOP_GE,
    BINOP_AND,
    BINOP_OR,
} BinOp;

typedef enum {
    UNOP_NEG,
    UNOP_NOT,
    UNOP_LEN,
} UnOp;

typedef enum {
    EXPR_NIL,
    EXPR_BOOLEAN,
    EXPR_INT,
    EXPR_STRING,
    // A slot in the current function's frame.
    EXPR_LOCAL,
    // A top-level `const` read in a function: an index into the run's
    // constants (`Program.const_names`).
    EXPR_CONSTANT,
    EXPR_INDEX,
    EXPR_CALL,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_LIST,
    EXPR_MAP,
    EXPR_EMPTY_TABLE,
} ExprKind;

typedef enum {
    TARGET_BUILTIN,
    // An index into `Program.functions`, set as the call is parsed: a
    // function is called only after it is declared.
    TARGET_FUNCTION,
    // `attempt(f, ...)`: the function `f`, an index as in TARGET_FUNCTION,
    // called with the rest of the arguments; its error becomes a value.
    TARGET_ATTEMPT,
} TargetKind;

typedef struct Expr Expr;

SLICE_TYPE(Exprs, Expr *);

typedef struct {
    Str key;
    Expr *value;
    Pos pos;
} MapEntry;

SLICE_TYPE(MapEntries, MapEntry);

typedef struct {
    TargetKind target;
    // The builtin, for TARGET_BUILTIN.
    Builtin builtin;
    // The function's index, for TARGET_FUNCTION and TARGET_ATTEMPT.
    uint32_t function;
    Exprs args;
} CallExpr;

typedef struct Expr {
    Pos pos;
    ExprKind kind;
    union {
        bool boolean;
        int64_t integer;
        Str string;
        uint16_t local;
        uint16_t constant;
        struct {
            Expr *obj;
            Expr *key;
        } index;
        CallExpr call;
        struct {
            BinOp op;
            Expr *left;
            Expr *right;
        } binary;
        struct {
            UnOp op;
            Expr *operand;
        } unary;
        Exprs list;
        MapEntries map;
    } as;
} Expr;

typedef enum {
    STMT_LOCAL,
    // `const NAME = e` at the top level: the script's own slot, and the index
    // of the constant that functions declared after it read.
    STMT_CONSTANT,
    STMT_ASSIGN,
    STMT_CALL,
    STMT_IF,
    STMT_WHILE,
    STMT_NUMERIC_FOR,
    STMT_LIST_FOR,
    STMT_RETURN,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_DO,
} StmtKind;

typedef struct Stmt Stmt;

SLICE_TYPE(Stmts, Stmt);

typedef struct {
    Expr *cond;
    Stmts body;
} Arm;

SLICE_TYPE(Arms, Arm);

typedef struct Stmt {
    Pos pos;
    StmtKind kind;
    union {
        // A `local`'s value is NULL when it has none.
        struct {
            uint16_t slot;
            Expr *value;
        } local;
        struct {
            uint16_t slot;
            uint16_t index;
            Expr *value;
        } constant;
        struct {
            Expr *target;
            Expr *value;
        } assign;
        Expr *call;
        // `has_otherwise` tells an `else` with no statements from none.
        struct {
            Arms arms;
            bool has_otherwise;
            Stmts otherwise;
        } if_;
        struct {
            Expr *cond;
            Stmts body;
        } while_;
        // A loop's step is NULL when it has none.
        struct {
            uint16_t slot;
            Expr *from;
            Expr *to;
            Expr *step;
            Stmts body;
        } numeric_for;
        struct {
            uint16_t slot;
            Expr *list;
            Stmts body;
        } list_for;
        // NULL for a bare `return`.
        Expr *return_;
        Stmts do_;
    } as;
} Stmt;

typedef struct {
    Str name;
    Pos pos;
    uint16_t params;
    // How many of the last `params` may be left out (`b?`). The parse passes
    // nil for each one a call leaves out.
    uint16_t optional;
    // Slots in its frame: parameters first, then every local it declares.
    uint16_t frame;
    Stmts body;
} Function;

SLICE_TYPE(Functions, Function);
SLICE_TYPE(StrList, Str);

typedef struct {
    // The script's functions and the prelude functions it calls, in the
    // order the parse met them.
    Functions functions;
    // The top-level `const`s, in order, by name.
    StrList const_names;
    // The script's own top level, a function with no parameters.
    Function main;
    // The names its `arg("NAME", ...)` calls read, each once, unless
    // `args_by_value`: one names it by a value other than a string literal,
    // so which names it reads cannot be said before it runs.
    StrList args_read;
    bool args_by_value;
} Program;

#endif
