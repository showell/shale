//! The host's Zig tools: `zig_rename`, `zig_decls`, `zig_tokens`, `zig_move` and
//! `zig_imports`, read by zig-ast (tools/zig_ast), Zig's own tokenizer and
//! parser in a program of its own. This file holds no Zig grammar: it checks a
//! call's arguments, reads the files through the store, sends them to zig-ast
//! (`ask`), and acts on the answer, in order: its charges, its stops (each
//! message said here), the values it gives the script, and the edits, written
//! through core's `rewrite`, so each change is diffed and logged as `edit`'s.
//! zig-ast writes no file.
//!
//! zig-ast runs once a run (`Proc`), started at the run's first Zig call and
//! ended with the host, so a recipe that reads a hundred files pays one
//! program, not a hundred. It is `tools:zig-ast/bin/zig-ast` (installed by
//! `shale tools/zig_ast/install.shale`), or the program `SHALE_ZIG_AST` names,
//! as this tree's own tests do.

const std = @import("std");
const webbox = @import("webbox");
const shale = @import("shale");
const script = shale.script;
const ast = script.ast;
const Run = script.interp.Run;
const Value = script.value.Value;
const Error = script.interp.Error;
const wire = @import("shale_zig_wire.zig");

pub const Span = script.builtins_change.Span;

const hl = @import("shale_host_linux.zig");

/// Per-call memory: the source, the request and the answer live in an arena
/// over the host's allocator, capped by what is left of the run's arena
/// budget (`Capped`), and freed when the call returns. Only what the script is
/// given, and what a change hands core, goes in the run's arena.
pub const Scratch = struct {
    capped: hl.Capped,
    arena: std.heap.ArenaAllocator,

    pub fn init(s: *Scratch, backing: std.mem.Allocator, run: *Run) std.mem.Allocator {
        s.capped = .{ .backing = backing, .most = run.budgets.arena_bytes -| run.budgeted.held };
        s.arena = std.heap.ArenaAllocator.init(s.capped.allocator());
        return s.arena.allocator();
    }

    pub fn deinit(s: *Scratch) void {
        s.arena.deinit();
    }
};

/// Copies `x` into the run's arena, so it outlives the scratch.
pub fn keep(run: *Run, pos: ast.Pos, x: []const u8) Error![]const u8 {
    return run.alloc.dupe(u8, x) catch run.oom(pos);
}

/// Lines, 1-based, inclusive.
pub const Lines = struct { first: usize, last: usize };

/// What a scoped rename reaches beyond a plain one's: a container's member,
/// or `prefix.from` through the given prefixes.
const Scope = union(enum) { container: []const u8, via: []const []const u8 };

// ── zig-ast, the process ──

/// The zig-ast process a run talks to: its pipes, and a reader over its
/// answers.
pub const Proc = struct {
    child: std.process.Child,
    io: std.Io,
    buf: [64 * 1024]u8 = undefined,
    reader: std.Io.File.Reader = undefined,

    /// Ends zig-ast: its stdin closed, it exits, and it is waited for.
    pub fn stop(p: *Proc) void {
        if (p.child.stdin) |f| f.close(p.io);
        p.child.stdin = null;
        _ = p.child.wait(p.io) catch {};
    }
};

/// The run's zig-ast, started if it is not yet. A start counts against the
/// `programs` budget, as any program does.
fn procOf(h: *hl.Host, run: *Run, pos: ast.Pos, who: []const u8) Error!*Proc {
    if (h.zig_ast) |p| return p;
    const exe = h.zig_ast_exe orelse exe: {
        for (h.env) |kv| if (std.mem.startsWith(u8, kv, "SHALE_ZIG_AST=")) break :exe kv["SHALE_ZIG_AST=".len..];
        for (h.mounts) |m| if (std.mem.eql(u8, m.name, "tools")) {
            const path = std.fs.path.join(run.alloc, &.{ m.dir, "zig-ast/bin/zig-ast" }) catch return run.oom(pos);
            std.Io.Dir.cwd().access(run.io, path, .{}) catch break;
            break :exe path;
        };
        return run.fail(pos, .host, "`{s}`: no tools:zig-ast/bin/zig-ast, the program that reads Zig: Shale's `shale tools/zig_ast/install.shale` builds it", .{who});
    };
    if (run.host_runs >= run.budgets.programs) return run.budgetStop(pos, .programs);
    run.host_runs += 1;
    const p = h.alloc.create(Proc) catch return run.oom(pos);
    const child = std.process.spawn(run.io, .{ .argv = &.{ exe, "--serve" }, .stdin = .pipe, .stdout = .pipe, .stderr = .pipe }) catch |e| {
        h.alloc.destroy(p);
        return run.fail(pos, .host, "`{s}`: zig-ast ({s}) did not start: {t}", .{ who, exe, e });
    };
    p.* = .{ .child = child, .io = run.io };
    p.reader = p.child.stdout.?.readerStreaming(run.io, &p.buf);
    h.zig_ast = p;
    const said = p.reader.interface.takeDelimiter('\n') catch null;
    if (said == null or !std.mem.startsWith(u8, said.?, wire.header)) return ended(p, run, pos, who, "its first line is not `" ++ wire.header ++ "<zig version>`");
    return p;
}

/// Stops the run for a zig-ast that went wrong, with what it said on stderr.
fn ended(p: *Proc, run: *Run, pos: ast.Pos, who: []const u8, why: []const u8) Error {
    var buf: [1024]u8 = undefined;
    var said: []const u8 = "";
    if (p.child.stderr) |f| {
        const n = f.readStreaming(p.io, &.{&buf}) catch 0;
        said = std.mem.trim(u8, buf[0..n], " \n");
    }
    return run.fail(pos, .host, "`{s}`: zig-ast {s}{s}{s}", .{ who, why, if (said.len > 0) ": " else "", said });
}

/// Sends a request, gives the answer's lines.
fn ask(h: *hl.Host, sa: std.mem.Allocator, run: *Run, pos: ast.Pos, who: []const u8, body: []const u8) Error!Answer {
    const p = try procOf(h, run, pos, who);
    const head = std.fmt.allocPrint(sa, "{d}\n", .{body.len}) catch return run.oom(pos);
    const in = p.child.stdin.?;
    in.writeStreamingAll(p.io, head) catch return ended(p, run, pos, who, "stopped taking requests");
    in.writeStreamingAll(p.io, body) catch return ended(p, run, pos, who, "stopped taking requests");
    const len = (p.reader.interface.takeDelimiter('\n') catch null) orelse return ended(p, run, pos, who, "ended before it answered");
    const n = wire.int(len) catch return ended(p, run, pos, who, "answered with no length");
    const got = p.reader.interface.readAllocAll(sa, n) catch return ended(p, run, pos, who, "ended in the middle of its answer");
    return .{ .lines = .{ .body = got }, .sa = sa, .run = run, .pos = pos, .who = who };
}

/// An answer's lines, read in order.
const Answer = struct {
    lines: wire.Lines,
    sa: std.mem.Allocator,
    run: *Run,
    pos: ast.Pos,
    who: []const u8,
    /// The fields of the line `next` gave last; the next call reuses them.
    into: [16][]const u8 = undefined,
    /// Each word kept in the run's arena once a call (a token's tag).
    kept: std.StringHashMapUnmanaged([]const u8) = .empty,

    /// The next line's fields: a `bad` line, or one that does not read, stops.
    fn next(an: *Answer) Error!?[]const []const u8 {
        const l = an.lines.next(an.sa, &an.into) catch |e| switch (e) {
            error.OutOfMemory => return an.run.oom(an.pos),
            else => return an.run.fail(an.pos, .host, "`{s}`: zig-ast's answer does not read ({t})", .{ an.who, e }),
        } orelse return null;
        if (l.len == 0) return an.wrong();
        if (std.mem.eql(u8, l[0], "bad")) return an.run.fail(an.pos, .host, "`{s}`: zig-ast refused the request: {s}", .{ an.who, if (l.len > 1) l[1] else "" });
        return l;
    }

    fn int(an: *Answer, l: []const []const u8, i: usize) Error!usize {
        if (i >= l.len) return an.wrong();
        return wire.int(l[i]) catch an.wrong();
    }

    fn field(an: *Answer, l: []const []const u8, i: usize) Error![]const u8 {
        if (i >= l.len) return an.wrong();
        return l[i];
    }

    /// `s` in the run's arena, copied once a call however often it comes.
    fn word(an: *Answer, s: []const u8) Error![]const u8 {
        const got = an.kept.getOrPut(an.sa, s) catch return an.run.oom(an.pos);
        if (!got.found_existing) {
            const k = try keep(an.run, an.pos, s);
            got.key_ptr.* = k;
            got.value_ptr.* = k;
        }
        return got.value_ptr.*;
    }

    fn wrong(an: *Answer) Error {
        return an.run.fail(an.pos, .host, "`{s}`: zig-ast's answer has a line this host does not know", .{an.who});
    }
};

fn is(l: []const []const u8, word: []const u8) bool {
    return std.mem.eql(u8, l[0], word);
}

/// A request's lines being written, in the scratch.
const Request = struct {
    out: std.Io.Writer.Allocating,
    run: *Run,
    pos: ast.Pos,

    fn init(sa: std.mem.Allocator, run: *Run, pos: ast.Pos, cmd: []const u8) Error!Request {
        var r: Request = .{ .out = .init(sa), .run = run, .pos = pos };
        try r.line(.{cmd});
        return r;
    }

    fn line(r: *Request, fields: anytype) Error!void {
        wire.line(&r.out.writer, fields) catch return r.run.oom(r.pos);
    }

    fn source(r: *Request, bytes: []const u8) Error!void {
        wire.source(&r.out.writer, bytes) catch return r.run.oom(r.pos);
    }
};

/// `was` with an answer's edits made, and the spans `rewrite` diffs by.
const Edited = struct { now: []const u8, spans: []const Span };

/// One edit of an answer: `was[o0..o1]` becomes `text`.
const Edit = struct { o0: usize, o1: usize, text: []const u8 };

fn applyEdits(sa: std.mem.Allocator, an: *Answer, was: []const u8, edits: []const Edit) Error!Edited {
    var out: std.ArrayList(u8) = .empty;
    var spans: std.ArrayList(Span) = .empty;
    var copied: usize = 0;
    for (edits) |e| {
        if (e.o0 < copied or e.o1 < e.o0 or e.o1 > was.len) return an.wrong();
        out.appendSlice(sa, was[copied..e.o0]) catch return an.run.oom(an.pos);
        const c0 = out.items.len;
        out.appendSlice(sa, e.text) catch return an.run.oom(an.pos);
        spans.append(sa, .{ .o0 = e.o0, .o1 = e.o1, .c0 = c0, .c1 = out.items.len }) catch return an.run.oom(an.pos);
        copied = e.o1;
    }
    out.appendSlice(sa, was[copied..]) catch return an.run.oom(an.pos);
    return .{ .now = out.items, .spans = spans.items };
}

fn editOf(an: *Answer, l: []const []const u8) Error!Edit {
    return .{ .o0 = try an.int(l, 1), .o1 = try an.int(l, 2), .text = try an.field(l, 3) };
}

/// Marks the run as trying a change and refuses it in a background run, before
/// anything is read, as the core's change builtins do.
fn changes(run: *Run, pos: ast.Pos, name: []const u8) Error!void {
    run.tried_change = true;
    if (run.read_only) return run.fail(pos, .argument, "`{s}` changes the store, and a background run only reads: send it as an `atomic` run", .{name});
}

// ── the builtins ──

/// `host("zig_rename", path, {from, to}, opts)`: reads, plans through zig-ast
/// and writes through core's `rewrite` if anything is renamed. Returns
/// `{renamed, left_in_comments}`. Charges the copy and a step per token.
pub fn rename(h: *hl.Host, run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    try changes(run, pos, "zig_rename");
    if (args.len != 3) return run.fail(pos, .argument, "`zig_rename` takes a path, `{{from, to}}` and options, given {d}", .{args.len});
    const hub = script.builtins_store;
    const rd = script.builtins_read;
    const p = try hub.pathArg(run, pos, .host, args[0], false);
    const names = switch (args[1]) {
        .table => |t| if (t.kind == .map) t else return run.fail(pos, .type, "`zig_rename`'s names are `{{from, to}}`", .{}),
        else => return run.fail(pos, .type, "`zig_rename`'s names are `{{from, to}}`", .{}),
    };
    // `{from, to}` (zig_rename) or `{many = {a = "x.a", ...}}` (zig_renames).
    const Rename = struct { from: []const u8, to: []const u8 };
    var renames: std.ArrayList(Rename) = .empty;
    if (names.map.get("many")) |many| {
        const m = switch (many) {
            .table => |t| t,
            else => return run.fail(pos, .type, "`zig_renames` takes a map of names, `{{a = \"x.a\"}}`, not {s}", .{many.typeName()}),
        };
        if (m.kind == .list) return run.fail(pos, .type, "`zig_renames` takes a map of names, `{{a = \"x.a\"}}`, not a list", .{});
        for (m.map.keys(), m.map.values()) |k, v| {
            const to_s = try script.builtins.stringArg(run, pos, .host, v, "a name's `to`");
            renames.append(run.alloc, .{ .from = k, .to = to_s }) catch return run.oom(pos);
        }
        if (renames.items.len == 0) return run.fail(pos, .argument, "`zig_renames` of no names: give `{{a = \"x.a\", ...}}`", .{});
    } else {
        const from = try script.builtins.stringArg(run, pos, .host, names.map.get("from") orelse .nil, "`from`");
        const to = try script.builtins.stringArg(run, pos, .host, names.map.get("to") orelse .nil, "`to`");
        renames.append(run.alloc, .{ .from = from, .to = to }) catch return run.oom(pos);
    }
    for (renames.items) |r| {
        if (!wire.isValidId(r.from)) return run.fail(pos, .argument, "`zig_rename`'s `from` (\"{s}\") is not a Zig identifier: one name, not a keyword", .{r.from});
        var parts = std.mem.splitScalar(u8, r.to, '.');
        while (parts.next()) |part| if (!wire.isValidId(part))
            return run.fail(pos, .argument, "`zig_rename`'s `to` (\"{s}\") is not a Zig identifier, or names joined by `.`", .{r.to});
    }
    const opts = try renameOpts(run, pos, args[2]);
    const lines = opts.lines;
    if (opts.scope != null and (renames.items.len != 1 or lines != null)) return run.fail(pos, .argument, "`zig_rename`'s `container` or `via` takes one name and no `lines`", .{});
    try hub.countRead(run, pos);
    const size = try rd.fileSize(run, pos, .host, p);
    const was = (try rd.readSized(run, pos, .host, p, size, 0, null)).string;
    const a = run.alloc;
    var scratch: Scratch = undefined;
    const sa = scratch.init(h.alloc, run);
    defer scratch.deinit();
    try script.builtins.chargeBytes(run, pos, was.len);
    var req = try Request.init(sa, run, pos, "rename");
    for (renames.items) |r| try req.line(.{ "name", r.from, r.to });
    if (lines) |l| try req.line(.{ "lines", l.first, l.last });
    if (opts.scope) |scope| switch (scope) {
        .container => |c| try req.line(.{ "container", c }),
        .via => |v| for (v) |prefix| try req.line(.{ "via", prefix }),
    };
    try req.source(was);
    var an = try ask(h, sa, run, pos, "zig_rename", req.out.written());
    var renamed: usize = 0;
    var left_in_comments: usize = 0;
    var edits: std.ArrayList(Edit) = .empty;
    var left: std.ArrayList(usize) = .empty;
    while (try an.next()) |l| {
        if (is(l, "work")) {
            try run.charge(pos, try an.int(l, 1));
        } else if (is(l, "renamed")) {
            renamed = try an.int(l, 1);
        } else if (is(l, "left_in_comments")) {
            left_in_comments = try an.int(l, 1);
        } else if (is(l, "s")) {
            edits.append(sa, try editOf(&an, l)) catch return run.oom(pos);
        } else if (is(l, "left")) {
            left.append(sa, try an.int(l, 1)) catch return run.oom(pos);
        } else if (is(l, "no_container")) {
            const scope = opts.scope.?;
            return run.fail(pos, .argument, "`zig_rename`: {s} declares no container `{s}` (`const {s} = struct {{...}}`){s}; nothing written", .{ p.rel(), scope.container, scope.container, if (try an.int(l, 1) == 2) ", or declares it twice" else "" });
        } else if (is(l, "no_member")) {
            return run.fail(pos, .argument, "`zig_rename`: {s}'s `{s}` declares no member `{s}`; nothing written", .{ p.rel(), opts.scope.?.container, renames.items[0].from });
        } else if (is(l, "unattributed")) {
            return run.fail(pos, .argument, "`zig_rename`: {s}:{d} has a `.{s}` that may or may not be `{s}`'s member: rename that one yourself, or rename it apart first; nothing written", .{ p.rel(), try an.int(l, 1), renames.items[0].from, opts.scope.?.container });
        } else if (is(l, "declaration")) {
            const name = try an.field(l, 2);
            var to: []const u8 = "";
            for (renames.items) |r| if (std.mem.eql(u8, r.from, name)) {
                to = r.to;
            };
            return run.fail(pos, .argument, "`zig_rename`: {s} declares `{s}` at line {d}, and `{s}` is qualified, which cannot be declared: rename the declaration itself, or leave it out with `lines`; nothing written", .{ p.rel(), name, try an.int(l, 1), to });
        } else if (is(l, "invalid")) {
            return run.fail(pos, .argument, "`zig_rename`: {s} does not tokenize as Zig at line {d}; nothing written", .{ p.rel(), try an.int(l, 1) });
        } else return an.wrong();
    }
    if (renamed > 0) {
        const done = try applyEdits(sa, &an, was, edits.items);
        try script.builtins_change.rewrite(run, pos, p, was, try keep(run, pos, done.now), a.dupe(Span, done.spans) catch return run.oom(pos));
    }
    const t = try run.newTable(pos);
    t.kind = .map;
    t.map.put(a, "renamed", .{ .int = @intCast(renamed) }) catch return run.oom(pos);
    t.map.put(a, "left_in_comments", .{ .int = @intCast(left_in_comments) }) catch return run.oom(pos);
    if (opts.scope != null and opts.scope.? == .via) {
        const lt = try run.newTable(pos);
        lt.kind = .list;
        for (left.items) |n| lt.list.append(a, .{ .int = @intCast(n) }) catch return run.oom(pos);
        t.map.put(a, "left", .{ .table = lt }) catch return run.oom(pos);
    }
    return .{ .table = t };
}

/// `zig_rename`'s options: `lines`, `container`, `via` (a prefix or a list).
fn renameOpts(run: *Run, pos: ast.Pos, v: Value) Error!struct { lines: ?Lines, scope: ?Scope } {
    const m = switch (v) {
        .nil => return .{ .lines = null, .scope = null },
        .table => |t| if (t.kind == .list) return run.fail(pos, .type, "`zig_rename`'s options are a map, not a list", .{}) else t,
        else => return run.fail(pos, .type, "`zig_rename`'s options are a map, not {s}", .{v.typeName()}),
    };
    var scope: ?Scope = null;
    var only_lines = try run.newTable(pos);
    only_lines.kind = .map;
    for (m.map.keys(), m.map.values()) |k, x| {
        if (std.mem.eql(u8, k, "container")) {
            if (scope != null) return run.fail(pos, .argument, "`zig_rename` takes `container` or `via`, not both", .{});
            const c = try script.builtins.stringArg(run, pos, .host, x, "`container`");
            if (!wire.isValidId(c)) return run.fail(pos, .argument, "`zig_rename`'s `container` (\"{s}\") is not a Zig identifier", .{c});
            scope = .{ .container = c };
        } else if (std.mem.eql(u8, k, "via")) {
            if (scope != null) return run.fail(pos, .argument, "`zig_rename` takes `container` or `via`, not both", .{});
            var list: std.ArrayList([]const u8) = .empty;
            switch (x) {
                .string => |s| list.append(run.alloc, s) catch return run.oom(pos),
                .table => |t| for (t.list.items) |e| list.append(run.alloc, try script.builtins.stringArg(run, pos, .host, e, "a `via` prefix")) catch return run.oom(pos),
                else => return run.fail(pos, .type, "`zig_rename`'s `via` is a prefix (\"search_index\") or a list of them, not {s}", .{x.typeName()}),
            }
            if (list.items.len == 0) return run.fail(pos, .argument, "`zig_rename`'s `via` is empty: give the prefixes the callers use", .{});
            for (list.items) |pre| {
                var parts = std.mem.splitScalar(u8, pre, '.');
                while (parts.next()) |part| if (!wire.isValidId(part)) return run.fail(pos, .argument, "`zig_rename`'s `via` prefix \"{s}\" is not names joined by `.`", .{pre});
            }
            scope = .{ .via = list.items };
        } else only_lines.map.put(run.alloc, k, x) catch return run.oom(pos);
    }
    return .{ .lines = try linesOpt(run, pos, .{ .table = only_lines }), .scope = scope };
}

/// Reads `p` into the scratch, charging the read and the copy.
fn readSource(sa: std.mem.Allocator, run: *Run, pos: ast.Pos, p: script.builtins_store.Path) Error![]const u8 {
    const hub = script.builtins_store;
    const rd = script.builtins_read;
    try hub.countRead(run, pos);
    const size = try rd.fileSize(run, pos, .host, p);
    const src = try rd.readSizedIn(sa, run, pos, .host, p, size, 0, null);
    try script.builtins.chargeBytes(run, pos, src.len);
    return src;
}

/// `host("zig_decls", path)`: a Zig file's top-level declarations as maps, in
/// file order, from zig-ast. Charges the read, the copy, and a step per token
/// and per Ast node. A parse error stops the run at its first line.
pub fn decls(h: *hl.Host, run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    if (args.len < 1 or args.len > 2) return run.fail(pos, .argument, "`zig_decls` takes a path and options, given {d} arguments", .{args.len});
    // `{container = name}` lists that container's members instead.
    var container: ?[]const u8 = null;
    if (args.len == 2) switch (args[1]) {
        .table => |t| if (t.kind == .map) {
            for (t.map.keys(), t.map.values()) |k, x| {
                if (!std.mem.eql(u8, k, "container")) return run.fail(pos, .argument, "`zig_decls` takes `container`, not `{s}`", .{k});
                container = switch (x) {
                    .string => |s| s,
                    else => return run.fail(pos, .type, "`zig_decls`' `container` is a name, not {s}", .{x.typeName()}),
                };
            }
        } else return run.fail(pos, .type, "`zig_decls`' options are a map, not a list", .{}),
        else => |v| return run.fail(pos, .type, "`zig_decls`' options are a map, not {s}", .{v.typeName()}),
    };
    const p = try script.builtins_store.pathArg(run, pos, .host, args[0], false);
    const a = run.alloc;
    var scratch: Scratch = undefined;
    const sa = scratch.init(h.alloc, run);
    defer scratch.deinit();
    const src = try readSource(sa, run, pos, p);
    var req = try Request.init(sa, run, pos, "decls");
    if (container) |c| try req.line(.{ "container", c });
    try req.source(src);
    var an = try ask(h, sa, run, pos, "zig_decls", req.out.written());
    const list = try run.newTable(pos);
    list.kind = .list;
    // The declaration whose `u` and `r` lines come next.
    var uses: ?*script.value.Table = null;
    var reaches: ?*script.value.Table = null;
    while (try an.next()) |l| {
        if (is(l, "work")) {
            try run.charge(pos, try an.int(l, 1));
        } else if (is(l, "d")) {
            if (l.len != 12) return an.wrong();
            const m = try run.newTable(pos);
            m.kind = .map;
            try put(run, pos, m, "kind", .{ .string = try an.word(l[1]) });
            try put(run, pos, m, "name", .{ .string = try keep(run, pos, l[2]) });
            try put(run, pos, m, "name_form", .{ .string = try an.word(l[3]) });
            try put(run, pos, m, "visibility", .{ .string = try an.word(l[4]) });
            try put(run, pos, m, "linkage", .{ .string = try an.word(l[5]) });
            inline for (.{ "first", "last", "start", "stop" }, 6..) |k, i| try put(run, pos, m, k, .{ .int = @intCast(try an.int(l, i)) });
            const above_first = try an.int(l, 10);
            if (above_first > 0) {
                const cm = try run.newTable(pos);
                cm.kind = .map;
                try put(run, pos, cm, "first", .{ .int = @intCast(above_first) });
                try put(run, pos, cm, "last", .{ .int = @intCast(try an.int(l, 11)) });
                try put(run, pos, m, "comment_above", .{ .table = cm });
            }
            const u = try run.newTable(pos);
            u.kind = .list;
            try put(run, pos, m, "uses", .{ .table = u });
            uses = u;
            if (container != null) {
                const r = try run.newTable(pos);
                r.kind = .list;
                try put(run, pos, m, "reaches", .{ .table = r });
                reaches = r;
            }
            list.list.append(a, .{ .table = m }) catch return run.oom(pos);
        } else if (is(l, "u") or is(l, "r")) {
            const into = (if (is(l, "u")) uses else reaches) orelse return an.wrong();
            into.list.append(a, .{ .string = try keep(run, pos, try an.field(l, 1)) }) catch return run.oom(pos);
        } else if (is(l, "parse_error")) {
            return run.fail(pos, .argument, "`zig_decls`: {s} does not parse as Zig: line {d} ({s})", .{ p.rel(), try an.int(l, 1), try an.field(l, 2) });
        } else if (is(l, "no_container")) {
            const name = container.?;
            return run.fail(pos, .argument, "`zig_decls`: {s} has no container `{s}` (a top-level `const {s} = struct {{...}}`, or a union's, an enum's, an opaque's)", .{ p.rel(), name, name });
        } else if (is(l, "invalid")) {
            return run.fail(pos, .argument, "`zig_decls`: {s} does not tokenize as Zig at line {d}", .{ p.rel(), try an.int(l, 1) });
        } else return an.wrong();
    }
    return .{ .table = list };
}

/// The text a token builtin's options keep, `{tag = name}`, or null for every
/// tag. `call` names the builtin in a refusal.
fn tagText(run: *Run, pos: ast.Pos, comptime call: []const u8, opts: Value) Error!?[]const u8 {
    var only: ?[]const u8 = null;
    switch (opts) {
        .table => |t| if (t.kind == .map) {
            for (t.map.keys(), t.map.values()) |k, x| {
                if (!std.mem.eql(u8, k, "tag")) return run.fail(pos, .argument, "`" ++ call ++ "` takes `tag`, not `{s}`", .{k});
                only = switch (x) {
                    .string => |v| v,
                    else => return run.fail(pos, .type, "`" ++ call ++ "`' `tag` is a tag's name, not {s}", .{x.typeName()}),
                };
            }
        } else return run.fail(pos, .type, "`" ++ call ++ "`' options are a map, not a list", .{}),
        else => |v| return run.fail(pos, .type, "`" ++ call ++ "`' options are a map, not {s}", .{v.typeName()}),
    }
    return only;
}

/// The tag a token builtin's options keep, `{tag = name}`, a name in `Tag`, or
/// null for every tag. `call` names the builtin in a refusal, and `tags` says
/// what a tag's name looks like.
pub fn tagOption(comptime Tag: type, run: *Run, pos: ast.Pos, comptime call: []const u8, comptime tags: []const u8, opts: Value) Error!?Tag {
    const s = (try tagText(run, pos, call, opts)) orelse return null;
    return std.meta.stringToEnum(Tag, s) orelse noTag(run, pos, call, tags, s);
}

fn noTag(run: *Run, pos: ast.Pos, comptime call: []const u8, comptime tags: []const u8, s: []const u8) Error {
    return run.fail(pos, .argument, "`" ++ call ++ "`: no token tag `{s}` (" ++ tags ++ ")", .{s});
}

/// `host("zig_tokens", path[, {tag}])`: the file's tokens from zig-ast, as
/// `{tag, text, line}`, where `tag` is a std.zig.Token.Tag name. A plain comment
/// is not a token; a doc comment is. `{tag = "identifier"}` keeps one kind, so
/// the script does not hold every token. Charges a step per token.
pub fn tokens(h: *hl.Host, run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    if (args.len < 1 or args.len > 2) return run.fail(pos, .argument, "`zig_tokens` takes a path and options, given {d} arguments", .{args.len});
    const only: ?[]const u8 = if (args.len == 2) try tagText(run, pos, "zig_tokens", args[1]) else null;
    const p = try script.builtins_store.pathArg(run, pos, .host, args[0], false);
    var scratch: Scratch = undefined;
    const sa = scratch.init(h.alloc, run);
    defer scratch.deinit();
    const src = try readSource(sa, run, pos, p);
    var req = try Request.init(sa, run, pos, "tokens");
    if (only) |t| try req.line(.{ "tag", t });
    try req.source(src);
    var an = try ask(h, sa, run, pos, "zig_tokens", req.out.written());
    const list = try run.newTable(pos);
    list.kind = .list;
    // Each tag's name, by its number, as the answer names it.
    var tag_names: [256]?[]const u8 = @splat(null);
    while (try an.next()) |l| {
        if (is(l, "work")) {
            try run.charge(pos, try an.int(l, 1));
        } else if (is(l, "tag")) {
            const i = try an.int(l, 1);
            if (i >= tag_names.len) return an.wrong();
            tag_names[i] = try keep(run, pos, try an.field(l, 2));
        } else if (is(l, "tb")) {
            const n = try an.int(l, 1);
            const block = an.lines.block(n * wire.token_bytes) catch return an.wrong();
            list.list.ensureUnusedCapacity(run.alloc, n) catch return run.oom(pos);
            for (0..n) |i| {
                const tk = wire.readToken(block, i);
                if (tk.end < tk.start or tk.end > src.len) return an.wrong();
                const m = try run.newTable(pos);
                m.kind = .map;
                try put(run, pos, m, "tag", .{ .string = tag_names[tk.tag] orelse return an.wrong() });
                try put(run, pos, m, "text", .{ .string = try keep(run, pos, src[tk.start..tk.end]) });
                try put(run, pos, m, "line", .{ .int = tk.line });
                list.list.appendAssumeCapacity(.{ .table = m });
            }
        } else if (is(l, "no_tag")) {
            return noTag(run, pos, "zig_tokens", "std.zig.Token.Tag: `identifier`, `keyword_fn`, `string_literal`, ...", only.?);
        } else if (is(l, "invalid")) {
            return run.fail(pos, .argument, "`zig_tokens`: {s} does not tokenize as Zig at line {d}", .{ p.rel(), try an.int(l, 1) });
        } else return an.wrong();
    }
    return .{ .table = list };
}

pub fn put(run: *Run, pos: ast.Pos, t: *script.value.Table, k: []const u8, v: Value) Error!void {
    t.map.put(run.alloc, k, v) catch return run.oom(pos);
}

/// A name and its line, as a move reports it; `target` for an import.
const NameLine = struct { name: []const u8, target: []const u8 = "", line: usize };

/// A list of `{name, line}` maps, or `{name, target, line}` if `targets`.
fn nameLines(run: *Run, pos: ast.Pos, items: []const NameLine, targets: bool) Error!Value {
    const list = try run.newTable(pos);
    list.kind = .list;
    for (items) |x| {
        const m = try run.newTable(pos);
        m.kind = .map;
        try put(run, pos, m, "name", .{ .string = try keep(run, pos, x.name) });
        if (targets) try put(run, pos, m, "target", .{ .string = try keep(run, pos, x.target) });
        try put(run, pos, m, "line", .{ .int = @intCast(x.line) });
        list.list.append(run.alloc, .{ .table = m }) catch return run.oom(pos);
    }
    return .{ .table = list };
}

/// The kinds a declaration to move may name (tools/zig_ast's `Kind`).
const kinds = [_][]const u8{ "fn", "const", "var", "test", "comptime", "field" };

/// `host("zig_move", from, to, {names, opts})`: moves top-level declarations
/// from one Zig file to another (zig-ast plans it) and reports what the move
/// broke. A missing `to` is created. `to` is written before `from`, so a stop
/// between them leaves the declarations in both, never in neither. Charges
/// the reads, the copies, and a step per token and node of both files.
pub fn move(h: *hl.Host, run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    try changes(run, pos, "zig_move");
    if (args.len != 3) return run.fail(pos, .argument, "`zig_move` takes `from`, `to` and `{{names, opts}}`, given {d}", .{args.len});
    const hub = script.builtins_store;
    const a = run.alloc;
    const p = try hub.pathArg(run, pos, .host, args[0], false);
    const q = try hub.pathArg(run, pos, .host, args[1], false);
    if (std.mem.eql(u8, p.full(), q.full())) return run.fail(pos, .argument, "`zig_move`: `from` and `to` are one file", .{});
    const packed_args = switch (args[2]) {
        .table => |t| if (t.kind == .map) t else return run.fail(pos, .type, "`zig_move`'s names are a list", .{}),
        else => return run.fail(pos, .type, "`zig_move`'s names are a list", .{}),
    };
    const names = switch (packed_args.map.get("names") orelse Value.nil) {
        .table => |t| if (t.kind == .list) t else return run.fail(pos, .type, "`zig_move`'s names are a list of names, or `{{kind = ..., name = ...}}`", .{}),
        else => return run.fail(pos, .type, "`zig_move`'s names are a list of names, or `{{kind = ..., name = ...}}`", .{}),
    };
    const Want = struct { name: []const u8, kind: ?[]const u8 = null };
    var wants: std.ArrayList(Want) = .empty;
    for (names.list.items) |v| switch (v) {
        .string => |s| wants.append(a, .{ .name = s }) catch return run.oom(pos),
        .table => |t| {
            if (t.kind != .map) return run.fail(pos, .type, "`zig_move`: a name is a string, or `{{kind = ..., name = ...}}`", .{});
            var w: Want = .{ .name = "" };
            for (t.map.keys(), t.map.values()) |k, x| {
                const s = switch (x) {
                    .string => |s| s,
                    else => return run.fail(pos, .type, "`zig_move`: a name's `{s}` is a string", .{k}),
                };
                if (std.mem.eql(u8, k, "name")) {
                    w.name = s;
                } else if (std.mem.eql(u8, k, "kind")) {
                    for (kinds) |kind| {
                        if (std.mem.eql(u8, kind, s)) w.kind = kind;
                    }
                    if (w.kind == null) return run.fail(pos, .argument, "`zig_move`: no kind `{s}`: fn, const, var, test, comptime or field", .{s});
                } else return run.fail(pos, .argument, "`zig_move`: a name takes `kind` and `name`, not `{s}`", .{k});
            }
            wants.append(a, w) catch return run.oom(pos);
        },
        else => return run.fail(pos, .type, "`zig_move`: a name is a string, or `{{kind = ..., name = ...}}`", .{}),
    };
    if (wants.items.len == 0) return run.fail(pos, .argument, "`zig_move`: no names: nothing to move", .{});
    var mode: []const u8 = "doc";
    var after: ?[]const u8 = null;
    // `from_as` names the import through which the moved code reaches `from`.
    var from_as: ?[]const u8 = null;
    switch (packed_args.map.get("opts") orelse Value.nil) {
        .nil => {},
        .table => |t| for (t.map.keys(), t.map.values()) |k, x| {
            const s = switch (x) {
                .string => |s| s,
                else => return run.fail(pos, .type, "`zig_move`'s `{s}` is a string", .{k}),
            };
            if (std.mem.eql(u8, k, "comment")) {
                if (!std.mem.eql(u8, s, "doc") and !std.mem.eql(u8, s, "doc_and_plain")) return run.fail(pos, .argument, "`zig_move`'s `comment` is \"doc\" or \"doc_and_plain\", not \"{s}\"", .{s});
                mode = s;
            } else if (std.mem.eql(u8, k, "after")) {
                after = s;
            } else if (std.mem.eql(u8, k, "from_as")) {
                if (!wire.isValidId(s)) return run.fail(pos, .argument, "`zig_move`'s `from_as` (\"{s}\") is not a Zig identifier", .{s});
                from_as = s;
            } else return run.fail(pos, .argument, "`zig_move` takes `comment`, `after` and `from_as`, not `{s}`", .{k});
        },
        else => |v| return run.fail(pos, .type, "`zig_move`'s options are a map, not {s}", .{v.typeName()}),
    }
    var scratch: Scratch = undefined;
    const sa = scratch.init(h.alloc, run);
    defer scratch.deinit();
    // Each file read into the run's arena, since `rewrite` is handed it.
    const from_src = try readKept(run, pos, p, false) orelse unreachable;
    const to_src = try readKept(run, pos, q, true) orelse "";
    var req = try Request.init(sa, run, pos, "move");
    for (wants.items) |w| if (w.kind) |k| try req.line(.{ "want", w.name, k }) else try req.line(.{ "want", w.name });
    try req.line(.{ "comment", mode });
    if (after) |name| try req.line(.{ "after", name });
    if (from_as) |fa| try req.line(.{ "from_as", fa, std.fs.path.basename(p.rel()) });
    try req.source(from_src);
    try req.source(to_src);
    var an = try ask(h, sa, run, pos, "zig_move", req.out.written());
    const Group = enum { to, from, to_q, to_i, from_p };
    var groups = std.EnumArray(Group, std.ArrayList(Edit)).initFill(.empty);
    var group: ?Group = null;
    var moved: usize = 0;
    var qualified: usize = 0;
    var made_pub: std.ArrayList([]const u8) = .empty;
    const Report = enum { needs_pub, needs_import, needs_from_from, uses_left };
    var reports = std.EnumArray(Report, std.ArrayList(NameLine)).initFill(.empty);
    while (try an.next()) |l| {
        if (is(l, "work")) {
            try run.charge(pos, try an.int(l, 1));
        } else if (is(l, "refused")) {
            return run.fail(pos, .argument, "`zig_move` {s} to {s}: {s}", .{ p.rel(), q.rel(), try keep(run, pos, try an.field(l, 1)) });
        } else if (is(l, "may_change")) {
            // Check both files may change before writing either.
            try script.builtins_change.mayChange(run, pos, q);
            try script.builtins_change.mayChange(run, pos, p);
        } else if (is(l, "stop")) {
            return run.fail(pos, .argument, "{s}", .{try an.field(l, 1)});
        } else if (is(l, "g")) {
            group = std.meta.stringToEnum(Group, try an.field(l, 1)) orelse return an.wrong();
        } else if (is(l, "s")) {
            const g = group orelse return an.wrong();
            groups.getPtr(g).append(sa, try editOf(&an, l)) catch return run.oom(pos);
        } else if (is(l, "moved")) {
            moved = try an.int(l, 1);
        } else if (is(l, "qualified")) {
            qualified = try an.int(l, 1);
        } else if (is(l, "made_pub")) {
            made_pub.append(sa, try an.field(l, 1)) catch return run.oom(pos);
        } else if (std.meta.stringToEnum(Report, l[0])) |r| {
            const targets = r == .needs_import;
            const x: NameLine = .{ .name = try an.field(l, 1), .target = if (targets) try an.field(l, 2) else "", .line = try an.int(l, if (targets) 3 else 2) };
            reports.getPtr(r).append(sa, x) catch return run.oom(pos);
        } else return an.wrong();
    }
    const to_done = try applyEdits(sa, &an, to_src, groups.get(.to).items);
    const from_done = try applyEdits(sa, &an, from_src, groups.get(.from).items);
    // `from_as` rewrites, each against the text the one before it left.
    const reach = from_as != null;
    const to_q: Edited = if (reach) try applyEdits(sa, &an, to_done.now, groups.get(.to_q).items) else undefined;
    const to_i: Edited = if (reach) try applyEdits(sa, &an, to_q.now, groups.get(.to_i).items) else undefined;
    const from_p: Edited = if (reach) try applyEdits(sa, &an, from_done.now, groups.get(.from_p).items) else undefined;
    const writes: u64 = if (reach) 5 else 2;
    // Check the budgets for every write up front, so a budget never stops a
    // move halfway.
    var bytes: u64 = to_done.now.len + from_done.now.len;
    if (reach) bytes += to_q.now.len + to_i.now.len + from_p.now.len;
    if (run.changes + writes > run.budgets.changes) return run.budgetStop(pos, .changes);
    if (bytes > run.budgets.change_bytes -| run.change_bytes) return run.budgetStop(pos, .change_bytes);
    const to_now = try keep(run, pos, to_done.now);
    const from_now = try keep(run, pos, from_done.now);
    try script.builtins_change.rewrite(run, pos, q, to_src, to_now, a.dupe(Span, to_done.spans) catch return run.oom(pos));
    // The store or the output budget can still stop the second write; the
    // message then says `to` already holds the copies.
    script.builtins_change.rewrite(run, pos, p, from_src, from_now, a.dupe(Span, from_done.spans) catch return run.oom(pos)) catch |e| {
        if (e == error.Stop) if (run.stop) |*s| {
            s.message = std.fmt.allocPrint(a, "{s}; `zig_move` wrote {s} first, so it holds the moved declarations, which {s} still has: take {s} back (`git checkout --`) before moving again", .{ s.message, q.rel(), p.rel(), q.rel() }) catch s.message;
        };
        return e;
    };
    if (reach) {
        const q_now = try keep(run, pos, to_q.now);
        try script.builtins_change.rewrite(run, pos, q, to_now, q_now, a.dupe(Span, to_q.spans) catch return run.oom(pos));
        try script.builtins_change.rewrite(run, pos, q, q_now, try keep(run, pos, to_i.now), a.dupe(Span, to_i.spans) catch return run.oom(pos));
        try script.builtins_change.rewrite(run, pos, p, from_now, try keep(run, pos, from_p.now), a.dupe(Span, from_p.spans) catch return run.oom(pos));
    }
    const t = try run.newTable(pos);
    t.kind = .map;
    try put(run, pos, t, "moved", .{ .int = @intCast(moved) });
    if (reach) {
        try put(run, pos, t, "qualified", .{ .int = @intCast(qualified) });
        const mp = try run.newTable(pos);
        mp.kind = .list;
        for (made_pub.items) |n| mp.list.append(a, .{ .string = try keep(run, pos, n) }) catch return run.oom(pos);
        try put(run, pos, t, "made_pub", .{ .table = mp });
    }
    try put(run, pos, t, "needs_pub", try nameLines(run, pos, reports.get(.needs_pub).items, false));
    try put(run, pos, t, "needs_import", try nameLines(run, pos, reports.get(.needs_import).items, true));
    try put(run, pos, t, "needs_from_from", try nameLines(run, pos, reports.get(.needs_from_from).items, false));
    try put(run, pos, t, "uses_left", try nameLines(run, pos, reports.get(.uses_left).items, false));
    return .{ .table = t };
}

/// Reads `p` into the run's arena, charging the read and the copy. Gives null
/// for a missing file when `missing_ok`.
fn readKept(run: *Run, pos: ast.Pos, p: script.builtins_store.Path, missing_ok: bool) Error!?[]const u8 {
    const hub = script.builtins_store;
    try hub.countRead(run, pos);
    if (missing_ok) {
        const st = webbox.store.statOrNull(run.io, run.alloc, p.full()) catch |e| return hub.storeFailed(run, pos, .host, p.rel(), e);
        if (st == null) return null;
    }
    const size = try script.builtins_read.fileSize(run, pos, .host, p);
    const was = try script.builtins_read.readSizedIn(run.alloc, run, pos, .host, p, size, 0, null);
    try script.builtins.chargeBytes(run, pos, was.len);
    return was;
}

/// `host("zig_imports", path)`: a Zig file's `@import`s and `@embedFile`s in
/// source order, from webbox's `zig_refs.zig` in zig-ast, as `{kind, target,
/// name, line, file}`. `file` is `found` or `missing` beside the importer, or
/// `module` for a target with no extension. Charges the read, the copy, a step
/// per token and node, and a step per call for the file check.
pub fn imports(h: *hl.Host, run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    if (args.len != 1) return run.fail(pos, .argument, "`zig_imports` takes a path, given {d} arguments", .{args.len});
    const a = run.alloc;
    const p = try script.builtins_store.pathArg(run, pos, .host, args[0], false);
    var scratch: Scratch = undefined;
    const sa = scratch.init(h.alloc, run);
    defer scratch.deinit();
    const src = try readSource(sa, run, pos, p);
    var req = try Request.init(sa, run, pos, "imports");
    try req.source(src);
    var an = try ask(h, sa, run, pos, "zig_imports", req.out.written());
    const dir = std.fs.path.dirnamePosix(p.full()) orelse ".";
    const list = try run.newTable(pos);
    list.kind = .list;
    while (try an.next()) |l| {
        if (is(l, "work")) {
            try run.charge(pos, try an.int(l, 1));
        } else if (is(l, "i")) {
            const target = try an.field(l, 2);
            const file: []const u8 = if (std.fs.path.extension(target).len == 0) "module" else file: {
                const full = std.fs.path.resolvePosix(sa, &.{ dir, target }) catch return run.oom(pos);
                const st = webbox.store.statOrNull(run.io, sa, full) catch |e| return script.builtins_store.storeFailed(run, pos, .host, try keep(run, pos, target), e);
                break :file if (st != null) "found" else "missing";
            };
            const m = try run.newTable(pos);
            m.kind = .map;
            try put(run, pos, m, "kind", .{ .string = try keep(run, pos, try an.field(l, 1)) });
            try put(run, pos, m, "target", .{ .string = try keep(run, pos, target) });
            try put(run, pos, m, "name", .{ .string = try keep(run, pos, try an.field(l, 3)) });
            try put(run, pos, m, "line", .{ .int = @intCast(try an.int(l, 4)) });
            try put(run, pos, m, "file", .{ .string = file });
            list.list.append(a, .{ .table = m }) catch return run.oom(pos);
        } else if (is(l, "parse_error")) {
            return run.fail(pos, .argument, "`zig_imports`: {s} does not parse as Zig: line {d}", .{ p.rel(), try an.int(l, 1) });
        } else return an.wrong();
    }
    return .{ .table = list };
}

fn linesOpt(run: *Run, pos: ast.Pos, v: Value) Error!?Lines {
    const m = switch (v) {
        .nil => return null,
        .table => |t| if (t.kind == .list) return run.fail(pos, .type, "`zig_rename`'s options are a map, not a list", .{}) else t,
        else => return run.fail(pos, .type, "`zig_rename`'s options are a map, not {s}", .{v.typeName()}),
    };
    var got: ?Lines = null;
    for (m.map.keys(), m.map.values()) |k, x| {
        if (!std.mem.eql(u8, k, "lines")) return run.fail(pos, .argument, "`zig_rename` takes `lines`, `container` and `via`, not `{s}`", .{k});
        const l = switch (x) {
            .table => |t| t,
            else => return run.fail(pos, .argument, "`zig_rename`'s `lines` is `{{first, last}}`", .{}),
        };
        if (l.kind != .list or l.list.items.len != 2 or l.list.items[0] != .int or l.list.items[1] != .int)
            return run.fail(pos, .argument, "`zig_rename`'s `lines` is `{{first, last}}`", .{});
        const first = l.list.items[0].int;
        const last = l.list.items[1].int;
        if (first < 1 or last < first) return run.fail(pos, .argument, "`zig_rename`'s `lines` ({d}, {d}) is no range: 1 <= first <= last", .{ first, last });
        got = .{ .first = @intCast(first), .last = @intCast(last) };
    }
    return got;
}
