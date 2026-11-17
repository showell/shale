//! **ZIG-AST: A ZIG FILE AS ZIG'S OWN TOKENIZER AND PARSER SEE IT**, for
//! Shale's `zig_decls`, `zig_tokens`, `zig_imports`, `zig_rename`,
//! `zig_renames` and `zig_move`. It is the one place Shale reads Zig with
//! `std.zig`: the host checks a call's arguments, reads the files, sends them
//! here, and acts on the answer (its values, its stops, its charges and the
//! edits it writes through its own `edit` machinery). zig-ast writes no file.
//!
//! `zig-ast --serve` runs for a whole Shale run: frames in on stdin, frames
//! out on stdout, until stdin ends (src/shale_zig_wire.zig has the framing).
//! A request's lines, then its sources:
//!
//!   `tokens`, `[tag <name>]`, one source.
//!   `decls`, `[container <name>]`, one source.
//!   `imports`, one source.
//!   `rename`, `name <from> <to>` (one or more), `[lines <first> <last>]`,
//!     `[container <c>]` or `via <prefix>` (one or more), one source.
//!   `move`, `want <name> [<kind>]` (one or more), `comment <mode>`,
//!     `[after <name>]`, `[from_as <name> <from's basename>]`, then two
//!     sources: `from`, then `to` (empty when it is missing).
//!
//! An answer is events, read in order: `work <n>` is n steps to charge;
//! a refusal is its own word and fields (`parse_error <line> <tag>`,
//! `invalid <line>`, `no_tag`, `no_container [<n>]`, `no_member`,
//! `unattributed <line>`, `declaration <line> <name>`, `refused <why>`, and
//! `stop <message>` for a refusal said whole); then the values:
//!
//!   tokens: `t <tag> <start> <end> <line>` a token.
//!   decls: `d <kind> <name> <name_form> <visibility> <linkage> <first>
//!     <last> <start> <stop> <above_first> <above_last>` (0 0 when there is
//!     no comment above), then its `u <name>` uses and `r <name>` reaches.
//!   imports: `i <kind> <target> <name> <line>`.
//!   rename: `renamed <n>`, `left_in_comments <n>`, `s <o0> <o1> <text>` an
//!     edit (`was[o0..o1]` becomes the text), `left <line>` (via).
//!   move: `may_change` (where the host asks both files), then for
//!     `from_as` its own events, then each write as `g <file>` and its `s`
//!     lines (`to`, `from`; then `to_q`, `to_i`, `from_p`, each against the
//!     text the one before it left), then `moved <n>`, `qualified <n>`,
//!     `made_pub <name>`, `needs_pub <name> <line>`, `needs_import <name>
//!     <target> <line>`, `needs_from_from <name> <line>`, `uses_left <name>
//!     <line>`.
//!
//! A request it cannot read is answered `bad <why>`, and the host stops.

const std = @import("std");
const wire = @import("zig_wire");
const zig_refs = @import("zig_refs");
const za = @import("zig_ast.zig");
const zm = @import("zig_move.zig");
const plan = @import("plan.zig");

pub fn main(init: std.process.Init) !void {
    const io = init.io;
    const args = try init.minimal.args.toSlice(init.arena.allocator());
    if (args.len != 2 or !std.mem.eql(u8, args[1], "--serve")) {
        std.debug.print("zig-ast: the one argument is --serve (Shale's host starts it; src/shale_zig_wire.zig)\n", .{});
        std.process.exit(2);
    }
    var in_buf: [64 * 1024]u8 = undefined;
    var out_buf: [64 * 1024]u8 = undefined;
    var in = std.Io.File.stdin().readerStreaming(io, &in_buf);
    var out = std.Io.File.stdout().writerStreaming(io, &out_buf);
    try out.interface.print("{s}{s}\n", .{ wire.header, @import("builtin").zig_version_string });
    try out.interface.flush();
    while (true) {
        var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
        defer arena.deinit();
        const a = arena.allocator();
        const len_line = (in.interface.takeDelimiter('\n') catch |e| switch (e) {
            error.ReadFailed => return in.err.?,
            error.StreamTooLong => return error.BadFrame,
        }) orelse return;
        const n = try wire.int(len_line);
        const body = try in.interface.readAllocAll(a, n);
        var answer: std.Io.Writer.Allocating = .init(a);
        answer.writer.flush() catch unreachable;
        serve(a, &answer.writer, body) catch |e| switch (e) {
            error.OutOfMemory => return e,
            error.WriteFailed => return error.OutOfMemory,
            else => {
                answer.clearRetainingCapacity();
                try wire.line(&answer.writer, .{ "bad", @errorName(e) });
            },
        };
        try out.interface.print("{d}\n", .{answer.written().len});
        try out.interface.writeAll(answer.written());
        try out.interface.flush();
    }
}

pub const ServeError = error{ OutOfMemory, WriteFailed, BadEscape, BadFrame, BadRequest, SpansIncomplete };

/// Answers one request's `body` on `w`.
pub fn serve(a: std.mem.Allocator, w: *std.Io.Writer, body: []const u8) ServeError!void {
    var lines: wire.Lines = .{ .body = body };
    const cmd = (try lines.next(a)) orelse return error.BadRequest;
    if (cmd.len != 1) return error.BadRequest;
    var opts: std.ArrayList([]const []const u8) = .empty;
    var sources: std.ArrayList([:0]const u8) = .empty;
    while (try lines.next(a)) |l| {
        if (l.len == 2 and std.mem.eql(u8, l[0], "src")) {
            try sources.append(a, try a.dupeSentinel(u8, try lines.block(try wire.int(l[1])), 0));
        } else try opts.append(a, l);
    }
    const name = cmd[0];
    const one = sources.items.len == 1;
    if (std.mem.eql(u8, name, "tokens") and one) return tokens(a, w, sources.items[0], opts.items);
    if (std.mem.eql(u8, name, "decls") and one) return decls(a, w, sources.items[0], opts.items);
    if (std.mem.eql(u8, name, "imports") and one) return imports(a, w, sources.items[0]);
    if (std.mem.eql(u8, name, "rename") and one) return rename(a, w, sources.items[0], opts.items);
    if (std.mem.eql(u8, name, "move") and sources.items.len == 2) return move(a, w, sources.items[0], sources.items[1], opts.items);
    return error.BadRequest;
}

/// The one option named `key`, its fields after the key, or null.
fn option(opts: []const []const []const u8, key: []const u8) ?[]const []const u8 {
    for (opts) |o| if (std.mem.eql(u8, o[0], key)) return o[1..];
    return null;
}

/// The option's one field.
fn option1(opts: []const []const []const u8, key: []const u8) ServeError!?[]const u8 {
    const o = option(opts, key) orelse return null;
    if (o.len != 1) return error.BadRequest;
    return o[0];
}

fn tokens(a: std.mem.Allocator, w: *std.Io.Writer, src: [:0]const u8, opts: []const []const []const u8) ServeError!void {
    const only: ?za.Tag = if (try option1(opts, "tag")) |t| std.meta.stringToEnum(za.Tag, t) orelse return wire.line(w, .{"no_tag"}) else null;
    const toks = switch (try za.tokens(a, src)) {
        .ok => |ok| ok,
        .invalid => |n| return wire.line(w, .{ "invalid", n }),
    };
    try wire.line(w, .{ "work", toks.len });
    const starts = try za.lineStarts(a, src);
    for (toks) |tk| {
        if (only) |o| if (tk.tag != o) continue;
        try wire.line(w, .{ "t", @tagName(tk.tag), tk.start, tk.end, za.lineOf(starts, tk.start) });
    }
}

fn sortNames(names: [][]const u8) void {
    std.mem.sort([]const u8, names, {}, struct {
        fn lt(_: void, x: []const u8, y: []const u8) bool {
            return std.mem.lessThan(u8, x, y);
        }
    }.lt);
}

/// A file's top-level declarations, or a container's members, each with its
/// `uses`: the top-level names in its span other than its own, distinct and
/// sorted, from `za.identifiers`, so a field, a member after `.`, a comment
/// and a string never count. In a container, also `reaches`: the members a
/// member touches through a `.`, by name, not by type.
fn decls(a: std.mem.Allocator, w: *std.Io.Writer, src: [:0]const u8, opts: []const []const []const u8) ServeError!void {
    const container = try option1(opts, "container");
    const root = switch (try za.decls(a, src)) {
        .ok => |ok| ok,
        .parse_error => |e| return wire.line(w, .{ "parse_error", e.line, e.tag }),
    };
    try wire.line(w, .{ "work", root.tokens + root.nodes });
    const got = if (container) |name| switch (try za.members(a, src, name)) {
        .ok => |ok| ok,
        .parse_error => unreachable, // the root parsed
        .no_container => return wire.line(w, .{"no_container"}),
    } else root;
    if (container != null) try wire.line(w, .{ "work", got.tokens + got.nodes });
    const toks = switch (try za.tokens(a, src)) {
        .ok => |ok| ok,
        .invalid => |n| return wire.line(w, .{ "invalid", n }),
    };
    const ids = try za.identifiers(a, toks);
    try wire.line(w, .{ "work", ids.len });
    // A test's name and a field are never used by name. In a container, we
    // drop the container itself, since every method names it.
    var top: std.StringHashMapUnmanaged(void) = .empty;
    for (root.decls) |d| if (d.name_form == .identifier and d.kind != .@"test" and d.kind != .field) {
        try top.put(a, d.name, {});
    };
    var member: std.StringHashMapUnmanaged(void) = .empty;
    if (container) |name| {
        _ = top.remove(name);
        for (got.decls) |d| if (d.name_form == .identifier and d.kind != .@"test") {
            try top.put(a, d.name, {});
            try member.put(a, d.name, {});
        };
    }
    var k: usize = 0;
    for (got.decls) |d| {
        var seen: std.StringHashMapUnmanaged(void) = .empty;
        var names: std.ArrayList([]const u8) = .empty;
        while (k < ids.len and ids[k].start < d.start) k += 1;
        var j = k;
        while (j < ids.len and ids[j].start < d.stop) : (j += 1) {
            const id = ids[j];
            if (id.role != .use) continue;
            const name = src[id.start..id.end];
            if (std.mem.eql(u8, name, d.name) or !top.contains(name) or seen.contains(name)) continue;
            try seen.put(a, name, {});
            try names.append(a, name);
        }
        sortNames(names.items);
        var reached: std.ArrayList([]const u8) = .empty;
        if (container != null) {
            var rseen: std.StringHashMapUnmanaged(void) = .empty;
            for (toks, 0..) |tk, ti| {
                if (tk.start < d.start or tk.start >= d.stop) continue;
                if (tk.tag != .identifier or ti == 0 or toks[ti - 1].tag != .period) continue;
                const name = src[tk.start..tk.end];
                if (std.mem.eql(u8, name, d.name) or !member.contains(name) or rseen.contains(name)) continue;
                try rseen.put(a, name, {});
                try reached.append(a, name);
            }
            sortNames(reached.items);
        }
        const above: za.Lines = d.comment_above orelse .{ .first = 0, .last = 0 };
        try wire.line(w, .{ "d", @tagName(d.kind), d.name, @tagName(d.name_form), @tagName(d.visibility), @tagName(d.linkage), d.first, d.last, d.start, d.stop, above.first, above.last });
        for (names.items) |n| try wire.line(w, .{ "u", n });
        for (reached.items) |n| try wire.line(w, .{ "r", n });
    }
}

/// A file's `@import`s and `@embedFile`s, from webbox's `zig_refs.zig`.
fn imports(a: std.mem.Allocator, w: *std.Io.Writer, src: [:0]const u8) ServeError!void {
    const got = switch (try zig_refs.refs(a, src)) {
        .ok => |ok| ok,
        .parse_error => |line| return wire.line(w, .{ "parse_error", line }),
    };
    try wire.line(w, .{ "work", got.tokens + got.nodes + got.refs.len });
    for (got.refs) |r| try wire.line(w, .{ "i", @tagName(r.kind), r.target, r.name, r.line });
}

/// Writes a planned text as its edits, after checking they are all of it:
/// outside the spans, `was` and `now` are the same bytes.
fn edits(w: *std.Io.Writer, was: []const u8, now: []const u8, spans: []const plan.Span) ServeError!void {
    var o: usize = 0;
    var c: usize = 0;
    for (spans) |s| {
        if (s.o0 < o or s.c0 < c or s.o0 - o != s.c0 - c or !std.mem.eql(u8, was[o..s.o0], now[c..s.c0])) return error.SpansIncomplete;
        try wire.line(w, .{ "s", s.o0, s.o1, now[s.c0..s.c1] });
        o = s.o1;
        c = s.c1;
    }
    if (!std.mem.eql(u8, was[o..], now[c..])) return error.SpansIncomplete;
}

fn rename(a: std.mem.Allocator, w: *std.Io.Writer, src: [:0]const u8, opts: []const []const []const u8) ServeError!void {
    var renames: std.ArrayList(plan.Rename) = .empty;
    var via: std.ArrayList([]const u8) = .empty;
    var lines: ?plan.Lines = null;
    var container: ?[]const u8 = null;
    for (opts) |o| {
        if (std.mem.eql(u8, o[0], "name") and o.len == 3) {
            try renames.append(a, .{ .from = o[1], .to = o[2] });
        } else if (std.mem.eql(u8, o[0], "lines") and o.len == 3) {
            lines = .{ .first = try wire.int(o[1]), .last = try wire.int(o[2]) };
        } else if (std.mem.eql(u8, o[0], "container") and o.len == 2) {
            container = o[1];
        } else if (std.mem.eql(u8, o[0], "via") and o.len == 2) {
            try via.append(a, o[1]);
        } else return error.BadRequest;
    }
    if (renames.items.len == 0) return error.BadRequest;
    const r = renames.items[0];
    var left: []const usize = &.{};
    const done: plan.Plan = if (container != null or via.items.len > 0) blk: {
        const s = if (container) |c| try plan.planMember(a, src, r.from, r.to, c) else try plan.planVia(a, src, r.from, r.to, via.items);
        switch (s) {
            .done => |d| {
                left = d.left;
                break :blk d.plan;
            },
            .invalid => |n| return wire.line(w, .{ "invalid", n }),
            .no_container => |n| return wire.line(w, .{ "no_container", n }),
            .no_member => return wire.line(w, .{"no_member"}),
            .unattributed => |n| return wire.line(w, .{ "unattributed", n }),
        }
    } else switch (try plan.planMany(a, src, renames.items, lines)) {
        .done => |d| d,
        .declaration => |d| return wire.line(w, .{ "declaration", d.line, d.name }),
        .invalid => |n| return wire.line(w, .{ "invalid", n }),
    };
    try wire.line(w, .{ "work", done.tokens });
    try wire.line(w, .{ "renamed", done.renamed });
    try wire.line(w, .{ "left_in_comments", done.left_in_comments });
    try edits(w, src, done.now, done.spans);
    for (left) |n| try wire.line(w, .{ "left", n });
}

fn move(a: std.mem.Allocator, w: *std.Io.Writer, from: [:0]const u8, to: [:0]const u8, opts: []const []const []const u8) ServeError!void {
    var wants: std.ArrayList(zm.Want) = .empty;
    var mode: zm.CommentMode = .doc;
    var after: ?[]const u8 = null;
    var from_as: ?[]const []const u8 = null;
    for (opts) |o| {
        if (std.mem.eql(u8, o[0], "want") and (o.len == 2 or o.len == 3)) {
            try wants.append(a, .{ .name = o[1], .kind = if (o.len == 3) std.meta.stringToEnum(za.Kind, o[2]) orelse return error.BadRequest else null });
        } else if (std.mem.eql(u8, o[0], "comment") and o.len == 2) {
            mode = std.meta.stringToEnum(zm.CommentMode, o[1]) orelse return error.BadRequest;
        } else if (std.mem.eql(u8, o[0], "after") and o.len == 2) {
            after = o[1];
        } else if (std.mem.eql(u8, o[0], "from_as") and o.len == 3) {
            from_as = o[1..];
        } else return error.BadRequest;
    }
    const done = switch (try zm.move(a, from, to, wants.items, mode, after)) {
        .done => |d| d,
        .refused => |why| return wire.line(w, .{ "refused", why }),
    };
    try wire.line(w, .{ "work", done.work });
    try wire.line(w, .{"may_change"});
    var reach: ?FromAs = null;
    if (from_as) |fa| reach = (try fromAs(a, w, done, fa[0], fa[1])) orelse return;
    try wire.line(w, .{ "g", "to" });
    try edits(w, to, done.to_now, done.to_spans);
    try wire.line(w, .{ "g", "from" });
    try edits(w, from, done.from_now, done.from_spans);
    if (reach) |r| {
        try wire.line(w, .{ "g", "to_q" });
        try edits(w, done.to_now, r.to_qualified, r.qualified_spans);
        try wire.line(w, .{ "g", "to_i" });
        try edits(w, r.to_qualified, r.to_imported, r.import_spans);
        try wire.line(w, .{ "g", "from_p" });
        try edits(w, done.from_now, r.from_pub, r.pub_spans);
    }
    try wire.line(w, .{ "moved", done.moved });
    if (reach) |r| {
        try wire.line(w, .{ "qualified", r.qualified });
        for (r.made_pub) |n| try wire.line(w, .{ "made_pub", n });
    }
    for (done.needs_pub) |x| try wire.line(w, .{ "needs_pub", x.name, x.line });
    for (done.needs_import) |x| try wire.line(w, .{ "needs_import", x.name, x.target, x.line });
    for (done.needs_from_from) |x| try wire.line(w, .{ "needs_from_from", x.name, x.line });
    for (done.uses_left) |x| try wire.line(w, .{ "uses_left", x.name, x.line });
}

/// The extra rewrites of a move with `from_as`, each with its spans against the
/// text before it: `done.to_now` to `to_qualified` to `to_imported`, and
/// `done.from_now` to `from_pub`.
const FromAs = struct {
    to_qualified: []const u8,
    qualified_spans: []const plan.Span,
    to_imported: []const u8,
    import_spans: []const plan.Span,
    from_pub: []const u8,
    pub_spans: []const plan.Span,
    qualified: usize,
    made_pub: []const []const u8,
};

/// Plans `from_as`'s rewrites, or writes the stop that refuses them and gives
/// null.
fn fromAs(a: std.mem.Allocator, w: *std.Io.Writer, done: zm.Moved, fa: []const u8, from_base: []const u8) ServeError!?FromAs {
    const to_z = try a.dupeSentinel(u8, done.to_now, 0);
    const toks = switch (try za.tokens(a, to_z)) {
        .ok => |ok| ok,
        .invalid => |n| return stop(a, w, "`zig_move`: the moved code does not tokenize at line {d}", .{n}),
    };
    for (toks, 0..) |tk, i| if (tk.tag == .identifier and std.mem.eql(u8, to_z[tk.start..tk.end], fa) and (i == 0 or toks[i - 1].tag != .period))
        return stop(a, w, "`zig_move`: `{s}` is a name in the moved code already: `from_as` would shadow it; nothing written", .{fa});
    // Qualify the moved code's uses of `from`'s names.
    var renames: std.ArrayList(plan.Rename) = .empty;
    var wanted: std.StringHashMapUnmanaged(void) = .empty;
    for (done.needs_from_from) |n| if (!wanted.contains(n.name)) {
        try wanted.put(a, n.name, {});
        try renames.append(a, .{ .from = n.name, .to = try std.fmt.allocPrint(a, "{s}.{s}", .{ fa, n.name }) });
    };
    const q = switch (try plan.planMany(a, to_z, renames.items, null)) {
        .done => |d| d,
        .declaration => |d| return stop(a, w, "`zig_move`: the moved code declares `{s}` (line {d}), which `from_as` would qualify; nothing written", .{ d.name, d.line }),
        .invalid => |n| return stop(a, w, "`zig_move`: the moved code does not tokenize at line {d}", .{n}),
    };
    try wire.line(w, .{ "work", q.tokens });
    // Insert the import after `to`'s last `@import`, or first if there is none.
    const qz = try a.dupeSentinel(u8, q.now, 0);
    const line = try std.fmt.allocPrint(a, "const {s} = @import(\"{s}\");", .{ fa, from_base });
    var at: usize = 0;
    switch (try za.decls(a, qz)) {
        .ok => |ok| for (ok.decls) |d| if (std.mem.startsWith(u8, d.value, "@import(")) {
            at = d.stop;
        },
        .parse_error => |e| return stop(a, w, "`zig_move`: the moved code does not parse at line {d}", .{e.line}),
    }
    const text = if (at == 0) try std.fmt.allocPrint(a, "{s}\n\n", .{line}) else try std.fmt.allocPrint(a, "\n{s}", .{line});
    const imported = try std.mem.concat(a, u8, &.{ q.now[0..at], text, q.now[at..] });
    const import_spans = try a.alloc(plan.Span, 1);
    import_spans[0] = .{ .o0 = at, .o1 = at, .c0 = at, .c1 = at + text.len };
    // Make those names `pub` in `from`, inserting past any doc comment.
    const fz = try a.dupeSentinel(u8, done.from_now, 0);
    var made: std.ArrayList([]const u8) = .empty;
    var offsets: std.ArrayList(usize) = .empty;
    switch (try za.decls(a, fz)) {
        .ok => |ok| for (ok.decls) |d| {
            if (!wanted.contains(d.name) or d.visibility == .@"pub") continue;
            if (d.kind != .@"const" and d.kind != .@"var" and d.kind != .@"fn") continue;
            var k = d.start;
            while (true) {
                var s = k;
                while (s < fz.len and (fz[s] == ' ' or fz[s] == '\t')) s += 1;
                if (!std.mem.startsWith(u8, fz[s..], "///")) break;
                k = (std.mem.indexOfScalarPos(u8, fz, s, '\n') orelse fz.len) + 1;
            }
            try offsets.append(a, k);
            try made.append(a, d.name);
        },
        .parse_error => |e| return stop(a, w, "`zig_move`: `from`, cut, does not parse at line {d}", .{e.line}),
    }
    var out: std.ArrayList(u8) = .empty;
    var spans: std.ArrayList(plan.Span) = .empty;
    var copied: usize = 0;
    for (offsets.items) |o| {
        try out.appendSlice(a, fz[copied..o]);
        const c0 = out.items.len;
        try out.appendSlice(a, "pub ");
        try spans.append(a, .{ .o0 = o, .o1 = o, .c0 = c0, .c1 = out.items.len });
        copied = o;
    }
    try out.appendSlice(a, fz[copied..]);
    sortNames(made.items);
    return .{ .to_qualified = q.now, .qualified_spans = q.spans, .to_imported = imported, .import_spans = import_spans, .from_pub = out.items, .pub_spans = spans.items, .qualified = q.renamed, .made_pub = made.items };
}

fn stop(a: std.mem.Allocator, w: *std.Io.Writer, comptime f: []const u8, args: anytype) ServeError!?FromAs {
    try wire.line(w, .{ "stop", try std.fmt.allocPrint(a, f, args) });
    return null;
}

test {
    _ = @import("zig_ast_test.zig");
}
