//! Reads a Zig file's declarations and identifiers with `std.zig.Ast` and the
//! tokenizer, for `zig_decls`, `zig_rename`, `zig_move` and `zig_imports`. Pure:
//! source in, answer out. The analysis is syntax and tokens, not types.

const std = @import("std");
const Ast = std.zig.Ast;

/// What a declaration is, by its leading keyword; `field` is a container field.
pub const Kind = enum { @"fn", @"const", @"var", @"test", @"comptime", field };

/// How a declaration is named: `identifier` (a declaration, a field,
/// `test foo {}`), `string` (`test "..."`, named by the string's text) or
/// `none` (`test {}`, `comptime`, named `""`).
pub const NameForm = enum { identifier, string, none };

/// Lines, 1-based, inclusive.
pub const Lines = struct { first: usize, last: usize };

/// A declaration of a container's top level. Its span runs from its doc
/// comment to the `}`, `;` or (for a field) `,` that ends it.
pub const Decl = struct {
    kind: Kind,
    /// The name's text; `""` when `name_form` is `none`.
    name: []const u8,
    name_form: NameForm,
    /// A field is always `pub`.
    visibility: enum { @"pub", file },
    linkage: enum { none, @"extern", @"export" },
    /// First line of the span, 1-based.
    first: usize,
    /// Last line of the span, 1-based, inclusive.
    last: usize,
    /// Byte offset of the span's start, inclusive.
    start: usize,
    /// Byte offset of the span's end, exclusive.
    stop: usize,
    /// The plain `//` lines directly above the span, if any.
    comment_above: ?Lines,
    /// The line its name is on (for a test or a comptime, its first token's).
    name_line: usize = 0,
    /// A `const`'s or `var`'s value, as its source says it; else "".
    value: []const u8 = "",
    /// When its value is a container, the names of its members that are not
    /// `pub`: what `X.y` from another file needs `pub` on. Fields are not listed.
    private_members: []const []const u8 = &.{},
};

/// Declarations read, and the tokens and nodes it took (the charge).
pub const Read = struct { decls: []const Decl, tokens: usize, nodes: usize };

/// The declarations, or the line and tag of the parser's first error; never a
/// partial list.
pub const Decls = union(enum) {
    ok: Read,
    parse_error: struct { line: usize, tag: []const u8 },
};

/// A file's top-level declarations, in file order.
pub fn decls(a: std.mem.Allocator, src: [:0]const u8) error{OutOfMemory}!Decls {
    const tree = try Ast.parse(a, src, .{ .mode = .zig });
    if (tree.errors.len != 0) {
        const e = tree.errors[0];
        return .{ .parse_error = .{ .line = tree.tokenLocation(0, e.token).line + 1, .tag = @tagName(e.tag) } };
    }
    const starts = try lineStarts(a, src);
    var out: std.ArrayList(Decl) = .empty;
    for (tree.rootDecls()) |n| try out.append(a, try declOf(a, &tree, src, starts, n));
    return .{ .ok = .{ .decls = out.items, .tokens = tree.tokens.len, .nodes = tree.nodes.len } };
}

/// The members of a container, or why there are none to give.
pub const Members = union(enum) {
    ok: Read,
    parse_error: struct { line: usize, tag: []const u8 },
    /// No top-level `const name = struct/union/enum/opaque {...}` by that name.
    no_container: void,
};

/// The declarations and fields of the top-level `const name = struct {...}` (or
/// union, enum, opaque), in order, read as the top level is. Used by
/// `zig_decls(p, {container = name})`.
pub fn members(a: std.mem.Allocator, src: [:0]const u8, name: []const u8) error{OutOfMemory}!Members {
    const tree = try Ast.parse(a, src, .{ .mode = .zig });
    if (tree.errors.len != 0) {
        const e = tree.errors[0];
        return .{ .parse_error = .{ .line = tree.tokenLocation(0, e.token).line + 1, .tag = @tagName(e.tag) } };
    }
    const starts = try lineStarts(a, src);
    for (tree.rootDecls()) |n| {
        const vd = tree.fullVarDecl(n) orelse continue;
        if (!std.mem.eql(u8, tree.tokenSlice(vd.ast.mut_token + 1), name)) continue;
        const init = vd.ast.init_node.unwrap() orelse continue;
        var buf: [2]Ast.Node.Index = undefined;
        const c = tree.fullContainerDecl(&buf, init) orelse continue;
        var out: std.ArrayList(Decl) = .empty;
        for (c.ast.members) |m| try out.append(a, try declOf(a, &tree, src, starts, m));
        return .{ .ok = .{ .decls = out.items, .tokens = tree.tokens.len, .nodes = tree.nodes.len } };
    }
    return .no_container;
}

/// One declaration, from its node: its kind, name, visibility, linkage and
/// span (its doc comment to its `;`, `}` or field's `,`).
fn declOf(a: std.mem.Allocator, tree: *const Ast, src: [:0]const u8, starts: []const usize, n: Ast.Node.Index) error{OutOfMemory}!Decl {
    var d: Decl = .{ .kind = .@"const", .name = "", .name_form = .identifier, .visibility = .file, .linkage = .none, .first = 0, .last = 0, .start = 0, .stop = 0, .comment_above = null };
    switch (tree.nodeTag(n)) {
        .fn_decl, .fn_proto, .fn_proto_multi, .fn_proto_one, .fn_proto_simple => {
            var buf: [1]Ast.Node.Index = undefined;
            const fp = tree.fullFnProto(&buf, n).?;
            d.kind = .@"fn";
            if (fp.name_token) |t| {
                d.name = tree.tokenSlice(t);
                d.name_line = lineOf(starts, tree.tokenStart(t));
            } else d.name_form = .none;
            if (fp.visib_token != null) d.visibility = .@"pub";
            if (fp.extern_export_inline_token) |t| d.linkage = linkageOf(tree.tokenTag(t));
        },
        .global_var_decl, .local_var_decl, .simple_var_decl, .aligned_var_decl => {
            const vd = tree.fullVarDecl(n).?;
            d.kind = if (tree.tokenTag(vd.ast.mut_token) == .keyword_const) .@"const" else .@"var";
            d.name = tree.tokenSlice(vd.ast.mut_token + 1);
            d.name_line = lineOf(starts, tree.tokenStart(vd.ast.mut_token + 1));
            if (vd.visib_token != null) d.visibility = .@"pub";
            if (vd.extern_export_token) |t| d.linkage = linkageOf(tree.tokenTag(t));
            if (vd.ast.init_node.unwrap()) |init| {
                const last_init = tree.lastToken(init);
                d.value = src[tree.tokenStart(tree.firstToken(init)) .. tree.tokenStart(last_init) + tree.tokenSlice(last_init).len];
                d.private_members = try privateMembers(a, tree, init);
            }
        },
        .test_decl => {
            d.kind = .@"test";
            if (tree.nodeData(n).opt_token_and_node[0].unwrap()) |t| {
                const s = tree.tokenSlice(t);
                if (tree.tokenTag(t) == .string_literal) {
                    d.name_form = .string;
                    d.name = s[1 .. s.len - 1];
                } else d.name = s;
            } else d.name_form = .none;
        },
        .@"comptime" => {
            d.kind = .@"comptime";
            d.name_form = .none;
        },
        .container_field_init, .container_field_align, .container_field => {
            const cf = tree.fullContainerField(n).?;
            d.kind = .field;
            d.visibility = .@"pub";
            d.name = tree.tokenSlice(cf.ast.main_token);
        },
        // A container's members are nothing else.
        else => unreachable,
    }
    // Widen the span back over its doc comment and on to its `;` or `,`.
    var first = tree.firstToken(n);
    while (first > 0 and tree.tokenTag(first - 1) == .doc_comment) first -= 1;
    var last = tree.lastToken(n);
    if (last + 1 < tree.tokens.len) switch (tree.tokenTag(last + 1)) {
        .semicolon => last += 1,
        .comma => if (d.kind == .field) {
            last += 1;
        },
        else => {},
    };
    if (d.name_line == 0) d.name_line = lineOf(starts, tree.tokenStart(tree.firstToken(n)));
    d.start = tree.tokenStart(first);
    d.stop = tree.tokenStart(last) + tree.tokenSlice(last).len;
    d.first = lineOf(starts, d.start);
    d.last = lineOf(starts, d.stop - 1);
    d.comment_above = commentAbove(src, starts, d.first);
    return d;
}

/// The names of a container value's declarations and functions that are not
/// `pub`.
fn privateMembers(a: std.mem.Allocator, tree: *const Ast, node: Ast.Node.Index) ![]const []const u8 {
    var buf: [2]Ast.Node.Index = undefined;
    const c = tree.fullContainerDecl(&buf, node) orelse return &.{};
    var out: std.ArrayList([]const u8) = .empty;
    for (c.ast.members) |m| {
        var fb: [1]Ast.Node.Index = undefined;
        if (tree.fullVarDecl(m)) |vd| {
            if (vd.visib_token == null) try out.append(a, tree.tokenSlice(vd.ast.mut_token + 1));
        } else if (tree.fullFnProto(&fb, m)) |fp| {
            if (fp.visib_token == null) if (fp.name_token) |t| try out.append(a, tree.tokenSlice(t));
        }
    }
    return out.items;
}

fn linkageOf(tag: std.zig.Token.Tag) @FieldType(Decl, "linkage") {
    return switch (tag) {
        .keyword_extern => .@"extern",
        .keyword_export => .@"export",
        else => .none,
    };
}

/// Where each line starts: `starts[i]` is line `i + 1`'s first byte.
pub fn lineStarts(a: std.mem.Allocator, src: []const u8) ![]const usize {
    var out: std.ArrayList(usize) = .empty;
    try out.append(a, 0);
    for (src, 0..) |c, i| if (c == '\n') try out.append(a, i + 1);
    return out.items;
}

/// The line, 1-based, of the byte at `offset`.
pub fn lineOf(starts: []const usize, offset: usize) usize {
    var lo: usize = 0;
    var hi: usize = starts.len;
    while (hi - lo > 1) {
        const mid = (lo + hi) / 2;
        if (starts[mid] <= offset) lo = mid else hi = mid;
    }
    return lo + 1;
}

/// The plain `//` lines directly above line `first` (no blank line
/// between, not `///` or `//!`), or null.
fn commentAbove(src: []const u8, starts: []const usize, first: usize) ?Lines {
    var top = first;
    while (top > 1) {
        const l = top - 1;
        const end = if (l < starts.len) starts[l] - 1 else src.len;
        const text = std.mem.trimStart(u8, src[starts[l - 1]..end], " \t");
        if (!std.mem.startsWith(u8, text, "//") or std.mem.startsWith(u8, text, "///") or std.mem.startsWith(u8, text, "//!")) break;
        top = l;
    }
    return if (top == first) null else .{ .first = top, .last = first - 1 };
}
pub const Tag = std.zig.Token.Tag;
/// A token's tag and byte range.
pub const Tok = struct { tag: Tag, start: usize, end: usize };

/// A file's tokens, or the line of the first one Zig's tokenizer refuses.
pub fn tokens(a: std.mem.Allocator, src: [:0]const u8) error{OutOfMemory}!union(enum) { ok: []const Tok, invalid: usize } {
    var toks: std.ArrayList(Tok) = .empty;
    var tz = std.zig.Tokenizer.init(src);
    while (true) {
        const t = tz.next();
        if (t.tag == .eof) break;
        if (t.tag == .invalid) return .{ .invalid = 1 + std.mem.count(u8, src[0..t.loc.start], "\n") };
        try toks.append(a, .{ .tag = t.tag, .start = t.loc.start, .end = t.loc.end });
    }
    return .{ .ok = toks.items };
}

/// What an identifier token is: a `use` of a name; a `declaration` (after
/// `const`, `var` or `fn`, before a `:` outside a container's own level, or in
/// a capture's `|...|`); or a `field`, a nested container's member (a field, an
/// enum, union or error set member, or the container's own `const`, `var` or
/// `fn`). A `field` may share a top-level name (`pub const kept_open =
/// &host_file.kept_open;`) and is never that declaration.
pub const Role = enum { use, declaration, field };

pub const Ident = struct { tok: usize, start: usize, end: usize, role: Role };

/// Every `.identifier` token not right after `.`, with its role. `zig_rename`
/// and `zig_move` both read identifiers only through this. Zig forbids a local
/// or parameter shadowing a top-level name, so a matching use or declaration is
/// that declaration's. `@"x"` is its own bytes. Two known false declarations
/// are safe, since a rename then stops with nothing written: `n` in
/// `buf[0..n :0]`, and `(a) | b | c` read as a capture.
pub fn identifiers(a: std.mem.Allocator, ts: []const Tok) error{OutOfMemory}![]const Ident {
    const Opens = enum { block, container, members };
    const Brace = struct { kind: Opens, parens: usize };
    var out: std.ArrayList(Ident) = .empty;
    var braces: std.ArrayList(Brace) = .empty;
    // What the next `{` opens: a container after `struct` or `opaque`;
    // members after `enum`, `union` or `error` (through `(enum)`, `(u8)`).
    var next_brace: Opens = .block;
    var parens: usize = 0;
    var in_capture = false;
    for (ts, 0..) |t, i| {
        const prev: ?Tag = if (i > 0) ts[i - 1].tag else null;
        const next: ?Tag = if (i + 1 < ts.len) ts[i + 1].tag else null;
        switch (t.tag) {
            .keyword_struct, .keyword_opaque => next_brace = .container,
            .keyword_enum, .keyword_union, .keyword_error => next_brace = .members,
            .l_paren => parens += 1,
            .r_paren => parens -|= 1,
            .doc_comment, .container_doc_comment => {},
            .l_brace => {
                try braces.append(a, .{ .kind = next_brace, .parens = parens });
                next_brace = .block;
            },
            .r_brace => {
                _ = braces.pop();
                next_brace = .block;
            },
            .pipe => {
                if (in_capture) {
                    in_capture = false;
                } else if (prev != null and (prev.? == .r_paren or prev.? == .keyword_else or prev.? == .keyword_catch or prev.? == .equal_angle_bracket_right) and isCapture(ts[i + 1 ..])) {
                    in_capture = true;
                }
                next_brace = .block;
            },
            .identifier => if (prev != .period) {
                // The file's root is a container.
                const top: Brace = if (braces.items.len > 0) braces.items[braces.items.len - 1] else .{ .kind = .container, .parens = 0 };
                const level = top.kind != .block and parens == top.parens;
                const member_start = prev == null or switch (prev.?) {
                    .l_brace, .comma, .doc_comment, .container_doc_comment, .semicolon, .r_brace, .keyword_comptime => true,
                    else => false,
                };
                const field = level and member_start and (next == .colon or
                    (top.kind == .members and (next == .comma or next == .r_brace or next == .equal)));
                // `const` qualifying a pointer (`[]const T`, `*align(4) const T`)
                // does not declare.
                const pointer_const = prev == .keyword_const and i >= 2 and switch (ts[i - 2].tag) {
                    .r_bracket, .asterisk, .r_paren, .keyword_allowzero, .keyword_volatile => true,
                    else => false,
                };
                const keyword = (prev == .keyword_const and !pointer_const) or prev == .keyword_var or prev == .keyword_fn;
                // A nested container's own declaration is its member.
                const member_decl = keyword and level and braces.items.len > 0;
                const role: Role = if (field or member_decl) .field else if ((prev == .keyword_const and !pointer_const) or prev == .keyword_var or prev == .keyword_fn or next == .colon or in_capture) .declaration else .use;
                try out.append(a, .{ .tok = i, .start = t.start, .end = t.end, .role = role });
            },
            else => next_brace = .block,
        }
    }
    return out.items;
}

/// Whether the tokens after a `|` are a capture's: `[*]name(, [*]name)*|`.
fn isCapture(ts: []const Tok) bool {
    var i: usize = 0;
    while (true) {
        if (i < ts.len and ts[i].tag == .asterisk) i += 1;
        if (i >= ts.len or ts[i].tag != .identifier) return false;
        i += 1;
        if (i < ts.len and ts[i].tag == .pipe) return true;
        if (i >= ts.len or ts[i].tag != .comma) return false;
        i += 1;
    }
}
