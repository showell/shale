// flat_tree.c: the Linux host's reading of a tree tool's `--flat` output.
// The Linux prelude's `roc_tree(paths)` and `go_tree(paths)` run roc-ast or
// go-ast and hand what it wrote to `host("roc_tree", out, ending)` or
// `host("go_tree", out, ending)`; the two tools write one format, so one
// reader takes both. The format's home is roc-ast's header
// (tools/roc_ast/main.zig): a `<tool> flat 1 <version>` line, then `==
// <path>` a file, and one fact a line, tab-separated, `n` a node (`parent`,
// `kind` and its span) and `a` an attribute (its node, `name` and `value`),
// with `\\`, `\t`, `\n` and `\r` escaped.
//
// Include run.h and builtins.c first.

// Which tool wrote the text: its name is in the header and the messages.
typedef enum {
    FLAT_ROC,
    FLAT_GO,
} FlatTool;

// How the tool's run ended. A run that exited 0 (`whole`)
// holds only whole lines, so a line that does not read is refused. One that
// stopped another way (`cut`) may end mid-line with its message after it, so
// such a line ends the trees and is given back as `said`.
typedef enum {
    FLAT_WHOLE,
    FLAT_CUT,
} FlatEnding;

typedef struct {
    Str name;
    Str value;
} FlatAttr;

VEC_DEFINE(FlatAttrs, FlatAttr)
VEC_DEFINE(FlatKids, size_t)

typedef struct {
    Str kind;
    size_t depth;
    // 1-based within its file; 0 for a root.
    size_t parent;
    FlatKids kids;
    // 0 for a node the tool gives no span (Roc's `args`, `statements`).
    size_t line;
    size_t col;
    size_t end_line;
    size_t end_col;
    FlatAttrs attrs;
} FlatNode;

VEC_DEFINE(FlatNodes, FlatNode)

typedef struct {
    Str path;
    FlatNodes nodes;
} FlatFile;

VEC_DEFINE(FlatFiles, FlatFile)

// What a reading found: the trees and the first line past them, or, when
// `bad`, the line that does not read as a tree's and why.
typedef struct {
    FlatFiles files;
    bool has_said;
    Str said;
    bool bad;
    size_t bad_line;
    Str why;
} FlatDump;

// The format this reader knows, as each tool's header says it.
#define FLAT_FORMAT "1"

// The tool's program, the builtin that reads it and the script that installs
// it, as its messages name them: Zig's `Tool.program`, `builtin`, `install`.
typedef struct {
    Str program;
    Str builtin;
    Str install;
} FlatNames;

static void flat_names(FlatTool tool, FlatNames *out) {
    if (tool == FLAT_ROC) {
        out->program = S("roc-ast");
        out->builtin = S("roc_tree");
        out->install = S("tools/roc_ast/install.shale");
        return;
    }
    out->program = S("go-ast");
    out->builtin = S("go_tree");
    out->install = S("tools/go_ast/install.shale");
}

// The names a node's own fields take, which no attribute may.
static Names flat_reserved(void) {
    static const Str names[] = {
        S_ITEM("kind"), S_ITEM("depth"), S_ITEM("parent"), S_ITEM("kids"),
        S_ITEM("line"), S_ITEM("col"), S_ITEM("end_line"), S_ITEM("end_col"),
    };
    Names n = {names, sizeof names / sizeof *names};
    return n;
}

// A number as the flat format writes it: an optional `+` or `-`, then
// decimal digits with `_` allowed between them, at most INT64_MAX, the most
// a Shale integer holds. A `-` is read before zeros alone, since any other
// digit makes the number negative. A number past INT64_MAX is a line that is
// not a tree's, a stop: Zig reads it as a usize and panics at its `@intCast`
// to an integer, but a tool's output is input, which never ends the process.
static bool flat_num(Str s, size_t *out) {
    Str d = s;
    bool neg = false;
    if (s.len > 0 && (str_byte(s, 0) == '+' || str_byte(s, 0) == '-')) {
        neg = str_byte(s, 0) == '-';
        d = str_sub(s, 1, s.len);
    }
    if (d.len == 0) return false;
    if (str_byte(d, 0) == '_' || str_byte(d, d.len - 1) == '_') return false;
    uint64_t v = 0;
    for (size_t k = 0; k < d.len; k++) {
        uint8_t c = str_byte(d, k);
        if (c == '_') continue;
        if (c < '0' || c > '9') return false;
        uint64_t digit = (uint64_t)(c - '0');
        if (neg) {
            if (digit != 0) return false;
            continue;
        }
        if (v > ((uint64_t)INT64_MAX - digit) / 10) return false;
        v = v * 10 + digit;
    }
    *out = (size_t)v;
    return true;
}

// Zig's `unescape`: `\\`, `\t`, `\n` and `\r` undone into `a`; a text with no
// `\` is itself. Any other escape, or a `\` at the end, sets `*bad`.
static STATUS_MUST_USE Status flat_unescape(Arena *a, Str s, Str *out, bool *bad) {
    *bad = false;
    if (str_find_byte(s, '\\') == NULL) {
        *out = s;
        return OK;
    }
    Buf b = buf_make(a);
    for (size_t k = 0; k < s.len; k++) {
        uint8_t c = str_byte(s, k);
        if (c != '\\') {
            TRY(buf_byte(&b, (char)c));
            continue;
        }
        k++;
        if (k >= s.len) {
            *bad = true;
            return OK;
        }
        uint8_t e = str_byte(s, k);
        char undone = 0;
        if (e == '\\') {
            undone = '\\';
        } else if (e == 't') {
            undone = '\t';
        } else if (e == 'n') {
            undone = '\n';
        } else if (e == 'r') {
            undone = '\r';
        } else {
            *bad = true;
            return OK;
        }
        TRY(buf_byte(&b, undone));
    }
    *out = buf_view(&b);
    return OK;
}

// Adds an attribute on node `node` (1-based) of `file`, `-` in its
// name read as `_`, never a node field's name, never twice on one node;
// `*why` says which rule it breaks, or stays empty.
static STATUS_MUST_USE Status flat_add_attr(Arena *a, FlatFile *file, size_t node, Str name, Str value, Str *why) {
    Str shown = {0};
    if (!arena_dup(a, name, &shown)) return STOP;
    Bytes room = {(char *)shown.ptr, shown.len};
    for (size_t k = 0; k < room.len; k++) {
        if (at(room, k) == '-') at(room, k) = '_';
    }
    Names reserved = flat_reserved();
    for (size_t k = 0; k < reserved.len; k++) {
        if (str_eq(at(reserved, k), shown)) {
            *why = S("an attribute takes a node field's name");
            return OK;
        }
    }
    FlatAttrs *attrs = &at(file->nodes, node - 1).attrs;
    for (size_t k = 0; k < attrs->len; k++) {
        if (str_eq(at(*attrs, k).name, shown)) {
            *why = S("an attribute twice in one node");
            return OK;
        }
    }
    FlatAttr x = {shown, value};
    return FlatAttrs_push(attrs, x);
}

SLICE_TYPE(FlatFields, Str);

// One node or attribute line, added to the last file.
// `*why` says why it does not read, or stays empty.
static STATUS_MUST_USE Status flat_tree_line(Arena *a, FlatFiles *files, Str raw, bool node_line, Str *why) {
    if (files->len == 0) {
        *why = S("a node before any `== path`");
        return OK;
    }
    FlatFile *file = &at(*files, files->len - 1);
    Str field_bytes[6];
    FlatFields f = {field_bytes, 6};
    size_t count = 0;
    Str rest = str_sub(raw, 2, raw.len);
    for (;;) {
        const char *tab = str_find_byte(rest, '\t');
        size_t end = tab == NULL ? rest.len : (size_t)(tab - rest.ptr);
        if (count == f.len) {
            *why = S("a line with too many fields");
            return OK;
        }
        at(f, count) = str_sub(rest, 0, end);
        count++;
        if (tab == NULL) break;
        rest = str_sub(rest, end + 1, rest.len);
    }
    bool bad = false;
    if (node_line) {
        if (count != 6) {
            *why = S("a node line without its six fields");
            return OK;
        }
        size_t parent = 0;
        if (!flat_num(at(f, 0), &parent)) {
            *why = S("a node's parent");
            return OK;
        }
        // DRAGON: memory. A parent is a number a program wrote, and one past
        // the nodes read would end the process at `at` below.
        // REMEDY: it is held to the nodes read before it is used.
        if (parent > file->nodes.len) {
            *why = S("a node's parent not yet read");
            return OK;
        }
        Str kind = {0};
        TRY(flat_unescape(a, at(f, 1), &kind, &bad));
        if (bad) {
            *why = S("a bad escape");
            return OK;
        }
        if (kind.len == 0) {
            *why = S("a node with no kind");
            return OK;
        }
        size_t span_bytes[4];
        SLICE_TYPE(FlatSpan, size_t);
        FlatSpan span = {span_bytes, 4};
        for (size_t k = 0; k < span.len; k++) {
            if (!flat_num(at(f, k + 2), &at(span, k))) {
                *why = S("a span's number");
                return OK;
            }
        }
        size_t depth = parent == 0 ? 0 : at(file->nodes, parent - 1).depth + 1;
        FlatNode nd = {kind, depth, parent, FlatKids_make(a), at(span, 0), at(span, 1), at(span, 2), at(span, 3), FlatAttrs_make(a)};
        TRY(FlatNodes_push(&file->nodes, nd));
        if (parent > 0) TRY(FlatKids_push(&at(file->nodes, parent - 1).kids, file->nodes.len));
        return OK;
    }
    if (count != 3) {
        *why = S("an attribute line without its three fields");
        return OK;
    }
    size_t node = 0;
    if (!flat_num(at(f, 0), &node)) {
        *why = S("an attribute's node");
        return OK;
    }
    // DRAGON: memory. An attribute's node is a number a program wrote; 0 or
    // one past the nodes read would end the process at `flat_add_attr`'s `at`.
    // REMEDY: it is held to the nodes read before it is used.
    if (node == 0 || node > file->nodes.len) {
        *why = S("an attribute's node not yet read");
        return OK;
    }
    Str name = {0};
    Str value = {0};
    TRY(flat_unescape(a, at(f, 1), &name, &bad));
    if (!bad) TRY(flat_unescape(a, at(f, 2), &value, &bad));
    if (bad) {
        *why = S("a bad escape");
        return OK;
    }
    return flat_add_attr(a, file, node, name, value, why);
}

// The header's refusal: none, or another format than this reader's.
static STATUS_MUST_USE Status flat_header_why(Arena *a, FlatTool tool, bool another, Str *why) {
    FlatNames n;
    flat_names(tool, &n);
    Buf b = buf_make(a);
    if (another) {
        TRY(buf_str(&b, n.program));
        TRY(buf_lit(&b, "'s --flat is another format than " FLAT_FORMAT ", the one this Shale reads: Shale's `shale "));
        TRY(buf_str(&b, n.install));
        TRY(buf_lit(&b, "` installs the matching "));
        TRY(buf_str(&b, n.program));
    } else {
        TRY(buf_lit(&b, "no `"));
        TRY(buf_str(&b, n.program));
        TRY(buf_lit(&b, " flat " FLAT_FORMAT "` header: an older "));
        TRY(buf_str(&b, n.program));
        TRY(buf_lit(&b, ", or not its --flat; Shale's `shale "));
        TRY(buf_str(&b, n.install));
        TRY(buf_lit(&b, "` installs the one this Shale reads"));
    }
    *why = buf_view(&b);
    return OK;
}

// Each `== path` and its nodes, and the first line past
// the trees, if any, into `a`; strings point into `text` or `a`. STOP only
// when `a` refuses.
static STATUS_MUST_USE Status flat_parse(Arena *a, Str text, FlatTool tool, FlatEnding ending, FlatDump *out) {
    memset(out, 0, sizeof *out);
    out->files = FlatFiles_make(a);
    FlatNames names;
    flat_names(tool, &names);
    Buf said_buf = buf_make(a);
    TRY(buf_str(&said_buf, names.program));
    TRY(buf_lit(&said_buf, " flat "));
    Str said = buf_view(&said_buf);
    bool header = false;
    size_t n = 0;
    Str rest = text;
    bool more = true;
    while (more) {
        // Zig's `splitScalar` on `\n`: what follows the last `\n` is a line.
        const char *nl = str_find_byte(rest, '\n');
        size_t end = nl == NULL ? rest.len : (size_t)(nl - rest.ptr);
        Str raw = str_sub(rest, 0, end);
        if (nl == NULL) {
            more = false;
        } else {
            rest = str_sub(rest, end + 1, rest.len);
        }
        n++;
        if (raw.len == 0) continue;
        if (!header) {
            // Whose format, before anything is read as a tree.
            bool another = false;
            if (str_starts_with(raw, said)) {
                Str words = str_sub(raw, said.len, raw.len);
                const char *sp = str_find_byte(words, ' ');
                Str first = str_sub(words, 0, sp == NULL ? words.len : (size_t)(sp - words.ptr));
                if (str_eq(first, S(FLAT_FORMAT))) {
                    header = true;
                    continue;
                }
                another = true;
            }
            out->bad = true;
            out->bad_line = n;
            return flat_header_why(a, tool, another, &out->why);
        }
        if (str_starts_with(raw, S("== "))) {
            FlatFile f = {str_sub(raw, 3, raw.len), FlatNodes_make(a)};
            TRY(FlatFiles_push(&out->files, f));
            continue;
        }
        bool node_line = str_starts_with(raw, S("n\t"));
        if (!node_line && !str_starts_with(raw, S("a\t"))) {
            out->has_said = true;
            out->said = raw;
            return OK;
        }
        Str why = S("");
        TRY(flat_tree_line(a, &out->files, raw, node_line, &why));
        if (why.len == 0) continue;
        if (ending == FLAT_WHOLE) {
            out->bad = true;
            out->bad_line = n;
            out->why = why;
            return OK;
        }
        out->has_said = true;
        out->said = raw;
        return OK;
    }
    return OK;
}

// `s` copied into the run's values, as a map's value under `key`.
static STATUS_MUST_USE Status flat_put_text(Run *r, Pos pos, Table *t, Str key, Str s) {
    Str kept = {0};
    if (!arena_dup(r->ctx.values, s, &kept)) return run_values_oom(r, pos);
    return run_map_put_str(r, pos, t, key, kept);
}

// A size the reader counted, as a Shale integer.
// DRAGON: the integers. A number past INT64_MAX would reach the script as a
// negative integer, the cast keeping its bits.
// REMEDY: flat_num refuses a line or column past INT64_MAX as a line that is
// not a tree's; a depth, a parent and a kid are counts of nodes held in
// memory, far below it. One past it here is a bug in the C, which aborts.
static int64_t flat_int(size_t n) {
    if ((uint64_t)n > (uint64_t)INT64_MAX) c_bug("flat_tree: a count past what an integer holds");
    return (int64_t)n;
}

// One node as Shale's map: `{kind, depth, parent, kids, line, col, end_line,
// end_col}` in that order, then its attributes in theirs.
static STATUS_MUST_USE Status flat_node_value(Run *r, Pos pos, const FlatNode *nd, Value *out) {
    Table *m = NULL;
    TRY(run_new_table(r, pos, &m));
    m->kind = TABLE_MAP;
    TRY(flat_put_text(r, pos, m, S("kind"), nd->kind));
    TRY(run_map_put_int(r, pos, m, S("depth"), flat_int(nd->depth)));
    TRY(run_map_put_int(r, pos, m, S("parent"), flat_int(nd->parent)));
    Table *kids = NULL;
    TRY(run_new_table(r, pos, &kids));
    kids->kind = TABLE_LIST;
    for (size_t k = 0; k < nd->kids.len; k++) {
        Value kv;
        value_int(&kv, flat_int(at(nd->kids, k)));
        TRY(run_list_push(r, pos, kids, &kv));
    }
    Value kids_v;
    value_table(&kids_v, kids);
    TRY(run_map_put(r, pos, m, S("kids"), &kids_v));
    TRY(run_map_put_int(r, pos, m, S("line"), flat_int(nd->line)));
    TRY(run_map_put_int(r, pos, m, S("col"), flat_int(nd->col)));
    TRY(run_map_put_int(r, pos, m, S("end_line"), flat_int(nd->end_line)));
    TRY(run_map_put_int(r, pos, m, S("end_col"), flat_int(nd->end_col)));
    for (size_t k = 0; k < nd->attrs.len; k++) {
        const FlatAttr *x = &at(nd->attrs, k);
        Str name = {0};
        if (!arena_dup(r->ctx.values, x->name, &name)) return run_values_oom(r, pos);
        TRY(flat_put_text(r, pos, m, name, x->value));
    }
    value_table(out, m);
    return OK;
}

// The dump as Shale's values, `{files = {{path, nodes}, ...}[, said]}`,
// charging a step a node, each file's before its nodes are made.
static STATUS_MUST_USE Status flat_dump_value(Run *r, Pos pos, const FlatDump *d, Value *out) {
    Table *files = NULL;
    TRY(run_new_table(r, pos, &files));
    files->kind = TABLE_LIST;
    for (size_t i = 0; i < d->files.len; i++) {
        const FlatFile *f = &at(d->files, i);
        TRY(run_charge(r, pos, f->nodes.len));
        Table *nodes = NULL;
        TRY(run_new_table(r, pos, &nodes));
        nodes->kind = TABLE_LIST;
        for (size_t k = 0; k < f->nodes.len; k++) {
            Value nv;
            TRY(flat_node_value(r, pos, &at(f->nodes, k), &nv));
            TRY(run_list_push(r, pos, nodes, &nv));
        }
        Table *fm = NULL;
        TRY(run_new_table(r, pos, &fm));
        fm->kind = TABLE_MAP;
        TRY(flat_put_text(r, pos, fm, S("path"), f->path));
        Value nodes_v;
        value_table(&nodes_v, nodes);
        TRY(run_map_put(r, pos, fm, S("nodes"), &nodes_v));
        Value fv;
        value_table(&fv, fm);
        TRY(run_list_push(r, pos, files, &fv));
    }
    Table *t = NULL;
    TRY(run_new_table(r, pos, &t));
    t->kind = TABLE_MAP;
    Value files_v;
    value_table(&files_v, files);
    TRY(run_map_put(r, pos, t, S("files"), &files_v));
    if (d->has_said) TRY(flat_put_text(r, pos, t, S("said"), d->said));
    value_table(out, t);
    return OK;
}

// The format texts, the fallback when a message cannot be made.
static Str flat_fallback(FlatTool tool, size_t which) {
    static const Str roc[] = {
        S_ITEM("`roc_tree`'s reading takes roc-ast's output and how its run ended, given {d} arguments"),
        S_ITEM("`roc_tree`'s reading takes how the run ended, \"whole\" or \"cut\", not \"{s}\""),
        S_ITEM("`roc_tree`'s reading takes how the run ended, \"whole\" or \"cut\", not {s}"),
        S_ITEM("`roc_tree`'s reading takes a text, not {s}"),
        S_ITEM("`roc_tree`: roc-ast's output line {d} is not a tree's: {s}"),
    };
    static const Str go[] = {
        S_ITEM("`go_tree`'s reading takes go-ast's output and how its run ended, given {d} arguments"),
        S_ITEM("`go_tree`'s reading takes how the run ended, \"whole\" or \"cut\", not \"{s}\""),
        S_ITEM("`go_tree`'s reading takes how the run ended, \"whole\" or \"cut\", not {s}"),
        S_ITEM("`go_tree`'s reading takes a text, not {s}"),
        S_ITEM("`go_tree`: go-ast's output line {d} is not a tree's: {s}"),
    };
    _Static_assert(sizeof roc / sizeof *roc == sizeof go / sizeof *go, "a Go text for every Roc one");
    Names n = {tool == FLAT_ROC ? roc : go, sizeof roc / sizeof *roc};
    return at(n, which);
}

enum {
    FLAT_SAID_ARGS,
    FLAT_SAID_ENDING,
    FLAT_SAID_ENDING_TYPE,
    FLAT_SAID_TEXT_TYPE,
    FLAT_SAID_NOT_A_TREE,
};

// What is read, once the arguments are checked: the text parsed in a scratch
// arena of its own, so the reading's own lists are no charge on the arena
// budget; only the values made from them are.
static STATUS_MUST_USE Status flat_read(Run *r, Pos pos, Str text, FlatTool tool, FlatEnding ending, Value *out) {
    FlatNames n;
    flat_names(tool, &n);
    // DRAGON: the arena budget. The reading's own lists are outside it, so a
    // tool's output holds a few times its bytes in memory no budget counts.
    // REMEDY: the text is a script's string, under the arena budget (the
    // prelude's `whole` keep, 64 MB, for a tool's output), and this arena's
    // own limit, 1 GiB, bounds the rest; its refusal is the arena budget's
    // stop (`run_values_oom`).
    Arena scratch;
    arena_init(&scratch, (size_t)1 << 30);
    FlatDump d;
    Status s = flat_parse(&scratch, text, tool, ending, &d);
    if (s != OK) {
        // The reading stops only when its arena refuses; any other STOP is a
        // bug in the C, not the arena budget.
        if (!scratch.hit) c_bug("flat_tree: a reading stopped with no refusal of its arena");
        s = run_values_oom(r, pos);
    } else if (d.bad) {
        s = run_fail_parts(r, pos, CLASS_ARGUMENT, flat_fallback(tool, FLAT_SAID_NOT_A_TREE),
                           (StrPieces){{S("`"), n.builtin, S("`: "), n.program, S("'s output line "), run_unum(r, d.bad_line), S(" is not a tree's: "), d.why}});
    } else {
        s = flat_dump_value(r, pos, &d, out);
    }
    // The scratch arena goes on every way out: what the values keep was
    // copied into the run's arena.
    arena_deinit(&scratch);
    return s;
}

// `host("roc_tree", out, ending)` and `host("go_tree", out, ending)`, the
// ending "whole" or "cut" (FlatEnding): `{files = {{path, nodes}, ...}[,
// said]}`, each node `{kind, depth, parent, kids, line, col, end_line,
// end_col}` and its attributes. Charges the text's bytes, then a step a node.
static STATUS_MUST_USE Status host_flat_tree(Run *r, Pos pos, ConstValues args, FlatTool tool, Value *out) {
    FlatNames n;
    flat_names(tool, &n);
    if (args.len != 2) {
        return run_fail_parts(r, pos, CLASS_ARGUMENT, flat_fallback(tool, FLAT_SAID_ARGS),
                              (StrPieces){{S("`"), n.builtin, S("`'s reading takes "), n.program, S("'s output and how its run ended, given "), run_unum(r, args.len), S(" arguments")}});
    }
    const Value *e = &at(args, 1);
    FlatEnding ending = FLAT_WHOLE;
    if (e->tag != VALUE_STRING) {
        return run_fail_parts(r, pos, CLASS_TYPE, flat_fallback(tool, FLAT_SAID_ENDING_TYPE),
                              (StrPieces){{S("`"), n.builtin, S("`'s reading takes how the run ended, \"whole\" or \"cut\", not "), value_type_name(e)}});
    }
    if (str_eq(e->as.string, S("cut"))) {
        ending = FLAT_CUT;
    } else if (!str_eq(e->as.string, S("whole"))) {
        return run_fail_parts(r, pos, CLASS_ARGUMENT, flat_fallback(tool, FLAT_SAID_ENDING),
                              (StrPieces){{S("`"), n.builtin, S("`'s reading takes how the run ended, \"whole\" or \"cut\", not \""), e->as.string, S("\"")}});
    }
    const Value *t = &at(args, 0);
    if (t->tag != VALUE_STRING) {
        return run_fail_parts(r, pos, CLASS_TYPE, flat_fallback(tool, FLAT_SAID_TEXT_TYPE),
                              (StrPieces){{S("`"), n.builtin, S("`'s reading takes a text, not "), value_type_name(t)}});
    }
    TRY(builtin_charge_bytes(r, pos, t->as.string.len));
    return flat_read(r, pos, t->as.string, tool, ending, out);
}
