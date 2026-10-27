//! The host's Zig tools over std.zig's tokenizer and parser: `zig_rename`,
//! `zig_decls`, `zig_tokens`, `zig_move` and `zig_imports`.
//!
//! A rename turns every identifier token `from` into `to`, but never one right
//! after `.` (a field, a member, an enum literal), never `@"from"`, and never in
//! a string, character or comment. The analysis is tokens, not types; the
//! compiler judges after. A qualified `to` (with a `.`) where `from` is declared
//! stops the run with nothing written, since `pub const m.from` is not Zig.
//! Planning is pure (`plan`); the write goes through core's `rewrite`.

const std = @import("std");
const webbox = @import("webbox");
const shale = @import("shale");
const script = shale.script;
const ast = script.ast;
const Run = script.interp.Run;
const Value = script.value.Value;
const Error = script.interp.Error;

pub const Span = script.builtins_change.Span;

/// Lines, 1-based, inclusive.
pub const Lines = struct { first: usize, last: usize };

/// A planned rename: the new text, the changed spans, the count renamed, the
/// whole-word matches left in comments (plain and doc), and the tokens read.
pub const Plan = struct { now: []const u8, spans: []const Span, renamed: usize, left_in_comments: usize, tokens: usize };

/// A plan, or where planning stopped: `declaration` is a qualified `to` where
/// `from` is declared; `invalid` is the line of a token the tokenizer refuses.
pub const Outcome = union(enum) { done: Plan, declaration: struct { line: usize, name: []const u8 }, invalid: usize };

/// One name and what it becomes.
pub const Rename = struct { from: []const u8, to: []const u8 };

const za = @import("shale_zig_ast.zig");
const hl = @import("shale_host_linux.zig");

/// Per-call memory for a parse: the NUL-terminated source, tokens, Ast and plan
/// live in an arena over the host's allocator, capped by what is left of the
/// run's arena budget (`Capped`), and freed when the call returns. Without it a
/// script calling `zig_decls` over a hundred files holds every parse at once.
/// Only what the script is given, and what a change hands core, goes in the
/// run's arena.
const Scratch = struct {
    capped: hl.Capped,
    arena: std.heap.ArenaAllocator,

    fn init(s: *Scratch, backing: std.mem.Allocator, run: *Run) std.mem.Allocator {
        s.capped = .{ .backing = backing, .most = run.budgets.arena_bytes -| run.budgeted.held };
        s.arena = std.heap.ArenaAllocator.init(s.capped.allocator());
        return s.arena.allocator();
    }

    fn deinit(s: *Scratch) void {
        s.arena.deinit();
    }
};

/// Copies `x` into the run's arena, so it outlives the scratch.
fn keep(run: *Run, pos: ast.Pos, x: []const u8) Error![]const u8 {
    return run.alloc.dupe(u8, x) catch run.oom(pos);
}

/// Plans renaming `from` to `to` in `src`, within `lines` if given. Pure; reads
/// identifiers through `za.identifiers`, as `zig_move` does.
pub fn plan(a: std.mem.Allocator, src: [:0]const u8, from: []const u8, to: []const u8, lines: ?Lines) error{OutOfMemory}!Outcome {
    return planMany(a, src, &.{.{ .from = from, .to = to }}, lines);
}

/// Plans several renames in one pass (`zig_renames`), each by `plan`'s rules.
/// Returns `declaration` for the first declaration a qualified `to` cannot take.
pub fn planMany(a: std.mem.Allocator, src: [:0]const u8, renames: []const Rename, lines: ?Lines) error{OutOfMemory}!Outcome {
    const ts = switch (try za.tokens(a, src)) {
        .ok => |ok| ok,
        .invalid => |n| return .{ .invalid = n },
    };
    var to_of: std.StringHashMapUnmanaged([]const u8) = .empty;
    for (renames) |r| try to_of.put(a, r.from, r.to);
    const starts = try za.lineStarts(a, src);
    // Count matches left in comments: plain ones sit in the gaps between
    // tokens, doc comments are tokens. Each name gets its own line cursor.
    var left: usize = 0;
    for (renames) |r| {
        var line: LineCursor = .{ .src = src };
        var gap: usize = 0;
        for (ts) |t| {
            left += try wordsIn(&line, src[gap..t.start], gap, r.from, lines);
            gap = t.end;
            if (t.tag == .doc_comment or t.tag == .container_doc_comment) left += try wordsIn(&line, src[t.start..t.end], t.start, r.from, lines);
        }
        left += try wordsIn(&line, src[gap..], gap, r.from, lines);
    }
    var out: std.ArrayList(u8) = .empty;
    var spans: std.ArrayList(Span) = .empty;
    var copied: usize = 0;
    var renamed: usize = 0;
    for (try za.identifiers(a, ts)) |id| {
        const name = src[id.start..id.end];
        const to = to_of.get(name) orelse continue;
        const qualified = std.mem.indexOfScalar(u8, to, '.') != null;
        const n = za.lineOf(starts, id.start);
        if (lines != null and (n < lines.?.first or n > lines.?.last)) continue;
        if (id.role == .field) continue;
        if (qualified and id.role == .declaration) return .{ .declaration = .{ .line = n, .name = name } };
        try out.appendSlice(a, src[copied..id.start]);
        const c0 = out.items.len;
        try out.appendSlice(a, to);
        try spans.append(a, .{ .o0 = id.start, .o1 = id.end, .c0 = c0, .c1 = out.items.len });
        copied = id.end;
        renamed += 1;
    }
    try out.appendSlice(a, src[copied..]);
    return .{ .done = .{ .now = out.items, .spans = spans.items, .renamed = renamed, .left_in_comments = left, .tokens = ts.len } };
}

/// What a scoped rename reaches beyond `plan`'s: a container's member, or
/// `prefix.from` through the given prefixes (`search_index`,
/// `harness.UidSite`).
pub const Scope = union(enum) { container: []const u8, via: []const []const u8 };

/// A scoped plan, or where it stopped: the container is not declared here,
/// or twice; it declares no `from`; a `.from` at `unattributed`'s line could
/// be the member and could be something else.
pub const Scoped = union(enum) {
    done: struct { plan: Plan, left: []const usize },
    invalid: usize,
    no_container: usize,
    no_member,
    unattributed: usize,
};

/// The whole-word `from`s in a file's comments, as `planMany` counts them.
fn commentsLeft(src: [:0]const u8, ts: []const za.Tok, from: []const u8) !usize {
    var left: usize = 0;
    var line: LineCursor = .{ .src = src };
    var gap: usize = 0;
    for (ts) |t| {
        left += try wordsIn(&line, src[gap..t.start], gap, from, null);
        gap = t.end;
        if (t.tag == .doc_comment or t.tag == .container_doc_comment) left += try wordsIn(&line, src[t.start..t.end], t.start, from, null);
    }
    return left + try wordsIn(&line, src[gap..], gap, from, null);
}

/// The plan of renaming the tokens at `at` (in order) to `to`.
fn planAt(a: std.mem.Allocator, src: [:0]const u8, ts: []const za.Tok, at: []const usize, to: []const u8, from: []const u8) !Plan {
    var out: std.ArrayList(u8) = .empty;
    var spans: std.ArrayList(Span) = .empty;
    var copied: usize = 0;
    for (at) |k| {
        const t = ts[k];
        try out.appendSlice(a, src[copied..t.start]);
        const c0 = out.items.len;
        try out.appendSlice(a, to);
        try spans.append(a, .{ .o0 = t.start, .o1 = t.end, .c0 = c0, .c1 = out.items.len });
        copied = t.end;
    }
    try out.appendSlice(a, src[copied..]);
    return .{ .now = out.items, .spans = spans.items, .renamed = at.len, .left_in_comments = try commentsLeft(src, ts, from), .tokens = ts.len };
}

fn isContainerKeyword(t: za.Tag) bool {
    return t == .keyword_struct or t == .keyword_union or t == .keyword_enum or t == .keyword_opaque;
}

/// Plans renaming member `from` of the container `container` declares in
/// this file (`const C = struct {...}`): its declaration, the bare uses
/// inside the container, and `C.from`, `self.from`, `Self.from` and
/// `@This().from` inside it. A `.from` through another container this file
/// declares is that one's, left alone; an enum literal `.from` is left
/// alone unless the container is an enum; any other `.from` stops the plan,
/// naming its line, rather than be guessed at. Pure.
pub fn planMember(a: std.mem.Allocator, src: [:0]const u8, from: []const u8, to: []const u8, container: []const u8) error{OutOfMemory}!Scoped {
    const ts = switch (try za.tokens(a, src)) {
        .ok => |ok| ok,
        .invalid => |n| return .{ .invalid = n },
    };
    const starts = try za.lineStarts(a, src);
    // The containers the file declares, and this one's braces.
    var others: std.StringHashMapUnmanaged(void) = .empty;
    var open: ?usize = null;
    var close: usize = 0;
    var is_enum = false;
    var found: usize = 0;
    var i: usize = 0;
    while (i + 3 < ts.len) : (i += 1) {
        if (ts[i].tag != .keyword_const or ts[i + 1].tag != .identifier or ts[i + 2].tag != .equal) continue;
        var j = i + 3;
        while (j < ts.len and (ts[j].tag == .keyword_extern or ts[j].tag == .keyword_packed)) j += 1;
        if (j >= ts.len or !isContainerKeyword(ts[j].tag)) continue;
        const name = src[ts[i + 1].start..ts[i + 1].end];
        if (!std.mem.eql(u8, name, container)) {
            try others.put(a, name, {});
            continue;
        }
        found += 1;
        if (found > 1) return .{ .no_container = 2 };
        is_enum = ts[j].tag == .keyword_enum;
        var b = j;
        while (b < ts.len and ts[b].tag != .l_brace) b += 1;
        var depth: usize = 0;
        var e = b;
        while (e < ts.len) : (e += 1) {
            if (ts[e].tag == .l_brace) depth += 1;
            if (ts[e].tag == .r_brace) {
                depth -= 1;
                if (depth == 0) break;
            }
        }
        open = b;
        close = e;
    }
    const o = open orelse return .{ .no_container = 0 };
    var at: std.ArrayList(usize) = .empty;
    var declared = false;
    var depth: usize = 0;
    for (ts, 0..) |t, k| {
        if (t.tag == .l_brace) depth += 1;
        if (t.tag == .r_brace) depth -|= 1;
        if (t.tag != .identifier or !std.mem.eql(u8, src[t.start..t.end], from)) continue;
        const inside = k > o and k < close;
        const prev: ?za.Tag = if (k > 0) ts[k - 1].tag else null;
        if (prev != .period) {
            if (!inside) continue;
            // A declaration directly in the container's braces.
            if (prev == .keyword_fn or prev == .keyword_const or prev == .keyword_var or (k + 1 < ts.len and ts[k + 1].tag == .colon)) declared = true;
            try at.append(a, k);
            continue;
        }
        // `.from`: through what?
        const r = if (k >= 2) ts[k - 2] else null;
        const rname: []const u8 = if (r != null and r.?.tag == .identifier) src[r.?.start..r.?.end] else "";
        if (std.mem.eql(u8, rname, container)) {
            try at.append(a, k);
        } else if (inside and (std.mem.eql(u8, rname, "self") or std.mem.eql(u8, rname, "Self"))) {
            try at.append(a, k);
        } else if (inside and r != null and r.?.tag == .r_paren and k >= 4 and ts[k - 3].tag == .l_paren and ts[k - 4].tag == .builtin and std.mem.eql(u8, src[ts[k - 4].start..ts[k - 4].end], "@This")) {
            try at.append(a, k);
        } else if (rname.len > 0 and others.contains(rname)) {
            // Another container's member of the same name.
        } else if (r == null or (r.?.tag != .identifier and r.?.tag != .r_paren and r.?.tag != .r_bracket and r.?.tag != .question_mark and r.?.tag != .period_asterisk)) {
            // An enum literal: the container's only if it is an enum.
            if (is_enum) return .{ .unattributed = za.lineOf(starts, t.start) };
        } else return .{ .unattributed = za.lineOf(starts, t.start) };
    }
    if (!declared) return .no_member;
    return .{ .done = .{ .plan = try planAt(a, src, ts, at.items, to, from), .left = &.{} } };
}

/// Plans renaming `from` where it follows one of `via`'s dotted prefixes
/// (`search_index.from`, `harness.UidSite.from`), the prefix not itself
/// after a `.`; every other `.from` is left, its line said. Pure.
pub fn planVia(a: std.mem.Allocator, src: [:0]const u8, from: []const u8, to: []const u8, via: []const []const u8) error{OutOfMemory}!Scoped {
    const ts = switch (try za.tokens(a, src)) {
        .ok => |ok| ok,
        .invalid => |n| return .{ .invalid = n },
    };
    const starts = try za.lineStarts(a, src);
    var at: std.ArrayList(usize) = .empty;
    var left: std.ArrayList(usize) = .empty;
    for (ts, 0..) |t, k| {
        if (t.tag != .identifier or !std.mem.eql(u8, src[t.start..t.end], from)) continue;
        if (k == 0 or ts[k - 1].tag != .period) continue;
        var hit = false;
        for (via) |prefix| {
            // `a.b` before `.from` is the tokens a, `.`, b, `.`, from.
            const n = std.mem.count(u8, prefix, ".") + 1;
            if (k < 2 * n) continue;
            const first = k - 2 * n;
            var parts = std.mem.splitScalar(u8, prefix, '.');
            var ok = true;
            var m: usize = 0;
            while (parts.next()) |part| : (m += 1) {
                const id = ts[first + 2 * m];
                if (id.tag != .identifier or !std.mem.eql(u8, src[id.start..id.end], part) or ts[first + 2 * m + 1].tag != .period) ok = false;
            }
            if (ok and (first == 0 or ts[first - 1].tag != .period)) hit = true;
        }
        if (hit) try at.append(a, k) else try left.append(a, za.lineOf(starts, t.start));
    }
    return .{ .done = .{ .plan = try planAt(a, src, ts, at.items, to, from), .left = left.items } };
}

/// Lines counted forward, for offsets that only grow.
const LineCursor = struct {
    src: []const u8,
    at: usize = 0,
    line: usize = 1,

    fn lineOf(c: *LineCursor, offset: usize) usize {
        c.line += std.mem.count(u8, c.src[c.at..offset], "\n");
        c.at = offset;
        return c.line;
    }
};

/// Counts whole-word `from`s in `text`, which is at `base` in the file, within
/// `lines`.
fn wordsIn(line: *LineCursor, text: []const u8, base: usize, from: []const u8, lines: ?Lines) !usize {
    var n: usize = 0;
    var i: usize = 0;
    while (std.mem.indexOfPos(u8, text, i, from)) |k| {
        i = k + 1;
        const end = k + from.len;
        if ((k > 0 and word(text[k - 1])) or (end < text.len and word(text[end]))) continue;
        const l = line.lineOf(base + k);
        if (lines == null or (l >= lines.?.first and l <= lines.?.last)) n += 1;
    }
    return n;
}

fn word(c: u8) bool {
    return std.ascii.isAlphanumeric(c) or c == '_';
}

/// Marks the run as trying a change and refuses it in a background run, before
/// anything is read, as the core's change builtins do.
fn changes(run: *Run, pos: ast.Pos, name: []const u8) Error!void {
    run.tried_change = true;
    if (run.read_only) return run.fail(pos, .argument, "`{s}` changes the store, and a background run only reads: send it as an `atomic` run", .{name});
}

/// `host("zig_rename", path, {from, to}, opts)`: reads, plans and writes
/// through core's `rewrite` if anything is renamed. Returns
/// `{renamed, left_in_comments}`. Charges the copy and a step per token.
pub fn rename(backing: std.mem.Allocator, run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
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
        if (!std.zig.isValidId(r.from)) return run.fail(pos, .argument, "`zig_rename`'s `from` (\"{s}\") is not a Zig identifier: one name, not a keyword", .{r.from});
        var parts = std.mem.splitScalar(u8, r.to, '.');
        while (parts.next()) |part| if (!std.zig.isValidId(part))
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
    const sa = scratch.init(backing, run);
    defer scratch.deinit();
    const src = sa.dupeSentinel(u8, was, 0) catch return run.oom(pos);
    try script.builtins.chargeBytes(run, pos, src.len);
    var left: []const usize = &.{};
    const got: Outcome = if (opts.scope) |scope| blk: {
        const r = renames.items[0];
        const s = switch (scope) {
            .container => |c| planMember(sa, src, r.from, r.to, c),
            .via => |v| planVia(sa, src, r.from, r.to, v),
        } catch return run.oom(pos);
        switch (s) {
            .done => |d| {
                left = d.left;
                break :blk .{ .done = d.plan };
            },
            .invalid => |n| break :blk .{ .invalid = n },
            .no_container => |n| return run.fail(pos, .argument, "`zig_rename`: {s} declares no container `{s}` (`const {s} = struct {{...}}`){s}; nothing written", .{ p.rel(), scope.container, scope.container, if (n == 2) ", or declares it twice" else "" }),
            .no_member => return run.fail(pos, .argument, "`zig_rename`: {s}'s `{s}` declares no member `{s}`; nothing written", .{ p.rel(), scope.container, r.from }),
            .unattributed => |n| return run.fail(pos, .argument, "`zig_rename`: {s}:{d} has a `.{s}` that may or may not be `{s}`'s member: rename that one yourself, or rename it apart first; nothing written", .{ p.rel(), n, r.from, scope.container }),
        }
    } else planMany(sa, src, renames.items, lines) catch return run.oom(pos);
    const done = switch (got) {
        .done => |d| d,
        .declaration => |d| {
            var to: []const u8 = "";
            for (renames.items) |r| if (std.mem.eql(u8, r.from, d.name)) {
                to = r.to;
            };
            return run.fail(pos, .argument, "`zig_rename`: {s} declares `{s}` at line {d}, and `{s}` is qualified, which cannot be declared: rename the declaration itself, or leave it out with `lines`; nothing written", .{ p.rel(), d.name, d.line, to });
        },
        .invalid => |n| return run.fail(pos, .argument, "`zig_rename`: {s} does not tokenize as Zig at line {d}; nothing written", .{ p.rel(), n }),
    };
    try run.charge(pos, done.tokens);
    if (done.renamed > 0) try script.builtins_change.rewrite(run, pos, p, was, try keep(run, pos, done.now), a.dupe(Span, done.spans) catch return run.oom(pos));
    const t = try run.newTable(pos);
    t.kind = .map;
    t.map.put(a, "renamed", .{ .int = @intCast(done.renamed) }) catch return run.oom(pos);
    t.map.put(a, "left_in_comments", .{ .int = @intCast(done.left_in_comments) }) catch return run.oom(pos);
    if (opts.scope != null and opts.scope.? == .via) {
        const l = try run.newTable(pos);
        l.kind = .list;
        for (left) |n| l.list.append(a, .{ .int = @intCast(n) }) catch return run.oom(pos);
        t.map.put(a, "left", .{ .table = l }) catch return run.oom(pos);
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
            if (!std.zig.isValidId(c)) return run.fail(pos, .argument, "`zig_rename`'s `container` (\"{s}\") is not a Zig identifier", .{c});
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
                while (parts.next()) |part| if (!std.zig.isValidId(part)) return run.fail(pos, .argument, "`zig_rename`'s `via` prefix \"{s}\" is not names joined by `.`", .{pre});
            }
            scope = .{ .via = list.items };
        } else only_lines.map.put(run.alloc, k, x) catch return run.oom(pos);
    }
    return .{ .lines = try linesOpt(run, pos, .{ .table = only_lines }), .scope = scope };
}

/// `host("zig_decls", path)`: a Zig file's top-level declarations as maps, in
/// file order, from `shale_zig_ast.zig`. Charges the read, the copy, and a step
/// per token and per Ast node. A parse error stops the run at its first line.
pub fn decls(backing: std.mem.Allocator, run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
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
    const hub = script.builtins_store;
    const rd = script.builtins_read;
    const p = try hub.pathArg(run, pos, .host, args[0], false);
    try hub.countRead(run, pos);
    const size = try rd.fileSize(run, pos, .host, p);
    const a = run.alloc;
    var scratch: Scratch = undefined;
    const sa = scratch.init(backing, run);
    defer scratch.deinit();
    const was = try rd.readSizedIn(sa, run, pos, .host, p, size, 0, null);
    const src = sa.dupeSentinel(u8, was, 0) catch return run.oom(pos);
    try script.builtins.chargeBytes(run, pos, src.len);
    const root = switch (za.decls(sa, src) catch return run.oom(pos)) {
        .ok => |ok| ok,
        .parse_error => |e| return run.fail(pos, .argument, "`zig_decls`: {s} does not parse as Zig: line {d} ({s})", .{ p.rel(), e.line, e.tag }),
    };
    try run.charge(pos, root.tokens + root.nodes);
    const got = if (container) |name| switch (za.members(sa, src, name) catch return run.oom(pos)) {
        .ok => |ok| ok,
        .parse_error => unreachable, // the root parsed
        .no_container => return run.fail(pos, .argument, "`zig_decls`: {s} has no container `{s}` (a top-level `const {s} = struct {{...}}`, or a union's, an enum's, an opaque's)", .{ p.rel(), name, name }),
    } else root;
    if (container != null) try run.charge(pos, got.tokens + got.nodes);
    // Each declaration's `uses`: the top-level names in its span other than its
    // own, distinct and sorted. Identifiers come from `za.identifiers`, so a
    // field, a member after `.`, a comment and a string never count.
    const toks = switch (za.tokens(sa, src) catch return run.oom(pos)) {
        .ok => |ok| ok,
        .invalid => |n| return run.fail(pos, .argument, "`zig_decls`: {s} does not tokenize as Zig at line {d}", .{ p.rel(), n }),
    };
    const ids = za.identifiers(sa, toks) catch return run.oom(pos);
    try run.charge(pos, ids.len);
    // A test's name and a field are never used by name. In a container, we
    // drop the container itself, since every method names it.
    var top: std.StringHashMapUnmanaged(void) = .empty;
    for (root.decls) |d| if (d.name_form == .identifier and d.kind != .@"test" and d.kind != .field) {
        top.put(sa, d.name, {}) catch return run.oom(pos);
    };
    var member: std.StringHashMapUnmanaged(void) = .empty;
    if (container) |name| {
        _ = top.remove(name);
        for (got.decls) |d| if (d.name_form == .identifier and d.kind != .@"test") {
            top.put(sa, d.name, {}) catch return run.oom(pos);
            member.put(sa, d.name, {}) catch return run.oom(pos);
        };
    }
    const list = try run.newTable(pos);
    list.kind = .list;
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
            seen.put(sa, name, {}) catch return run.oom(pos);
            names.append(sa, name) catch return run.oom(pos);
        }
        std.mem.sort([]const u8, names.items, {}, struct {
            fn lt(_: void, x: []const u8, y: []const u8) bool {
                return std.mem.lessThan(u8, x, y);
            }
        }.lt);
        const uses = try run.newTable(pos);
        uses.kind = .list;
        for (names.items) |n| uses.list.append(a, .{ .string = try keep(run, pos, n) }) catch return run.oom(pos);
        // In a container, the members a method reaches as `self.x` (or any
        // `v.x`), matched by name, not by type.
        var reached: std.ArrayList([]const u8) = .empty;
        if (container != null) {
            var rseen: std.StringHashMapUnmanaged(void) = .empty;
            for (toks, 0..) |tk, ti| {
                if (tk.start < d.start or tk.start >= d.stop) continue;
                if (tk.tag != .identifier or ti == 0 or toks[ti - 1].tag != .period) continue;
                const name = src[tk.start..tk.end];
                if (std.mem.eql(u8, name, d.name) or !member.contains(name) or rseen.contains(name)) continue;
                rseen.put(sa, name, {}) catch return run.oom(pos);
                reached.append(sa, name) catch return run.oom(pos);
            }
            std.mem.sort([]const u8, reached.items, {}, struct {
                fn lt(_: void, x: []const u8, y: []const u8) bool {
                    return std.mem.lessThan(u8, x, y);
                }
            }.lt);
        }
        const reaches = try run.newTable(pos);
        reaches.kind = .list;
        for (reached.items) |n| reaches.list.append(a, .{ .string = try keep(run, pos, n) }) catch return run.oom(pos);
        const m = try run.newTable(pos);
        m.kind = .map;
        try put(run, pos, m, "kind", .{ .string = @tagName(d.kind) });
        try put(run, pos, m, "name", .{ .string = try keep(run, pos, d.name) });
        try put(run, pos, m, "name_form", .{ .string = @tagName(d.name_form) });
        try put(run, pos, m, "visibility", .{ .string = @tagName(d.visibility) });
        try put(run, pos, m, "linkage", .{ .string = @tagName(d.linkage) });
        try put(run, pos, m, "first", .{ .int = @intCast(d.first) });
        try put(run, pos, m, "last", .{ .int = @intCast(d.last) });
        try put(run, pos, m, "start", .{ .int = @intCast(d.start) });
        try put(run, pos, m, "stop", .{ .int = @intCast(d.stop) });
        if (d.comment_above) |c| {
            const cm = try run.newTable(pos);
            cm.kind = .map;
            try put(run, pos, cm, "first", .{ .int = @intCast(c.first) });
            try put(run, pos, cm, "last", .{ .int = @intCast(c.last) });
            try put(run, pos, m, "comment_above", .{ .table = cm });
        }
        try put(run, pos, m, "uses", .{ .table = uses });
        if (container != null) try put(run, pos, m, "reaches", .{ .table = reaches });
        list.list.append(a, .{ .table = m }) catch return run.oom(pos);
    }
    return .{ .table = list };
}

/// `host("zig_tokens", path[, {tag}])`: the file's tokens from `za.tokens`, as
/// `{tag, text, line}`, where `tag` is a std.zig.Token.Tag name. A plain comment
/// is not a token; a doc comment is. `{tag = "identifier"}` keeps one kind, so
/// the script does not hold every token. Charges a step per token.
pub fn tokens(backing: std.mem.Allocator, run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    if (args.len < 1 or args.len > 2) return run.fail(pos, .argument, "`zig_tokens` takes a path and options, given {d} arguments", .{args.len});
    var only: ?za.Tag = null;
    if (args.len == 2) switch (args[1]) {
        .table => |t| if (t.kind == .map) {
            for (t.map.keys(), t.map.values()) |k, x| {
                if (!std.mem.eql(u8, k, "tag")) return run.fail(pos, .argument, "`zig_tokens` takes `tag`, not `{s}`", .{k});
                const s = switch (x) {
                    .string => |v| v,
                    else => return run.fail(pos, .type, "`zig_tokens`' `tag` is a tag's name, not {s}", .{x.typeName()}),
                };
                only = std.meta.stringToEnum(za.Tag, s) orelse return run.fail(pos, .argument, "`zig_tokens`: no token tag `{s}` (std.zig.Token.Tag: `identifier`, `keyword_fn`, `string_literal`, ...)", .{s});
            }
        } else return run.fail(pos, .type, "`zig_tokens`' options are a map, not a list", .{}),
        else => |v| return run.fail(pos, .type, "`zig_tokens`' options are a map, not {s}", .{v.typeName()}),
    };
    const hub = script.builtins_store;
    const rd = script.builtins_read;
    const p = try hub.pathArg(run, pos, .host, args[0], false);
    try hub.countRead(run, pos);
    const size = try rd.fileSize(run, pos, .host, p);
    var scratch: Scratch = undefined;
    const sa = scratch.init(backing, run);
    defer scratch.deinit();
    const was = try rd.readSizedIn(sa, run, pos, .host, p, size, 0, null);
    const src = sa.dupeSentinel(u8, was, 0) catch return run.oom(pos);
    try script.builtins.chargeBytes(run, pos, src.len);
    const toks = switch (za.tokens(sa, src) catch return run.oom(pos)) {
        .ok => |ok| ok,
        .invalid => |n| return run.fail(pos, .argument, "`zig_tokens`: {s} does not tokenize as Zig at line {d}", .{ p.rel(), n }),
    };
    try run.charge(pos, toks.len);
    const starts = za.lineStarts(sa, src) catch return run.oom(pos);
    const list = try run.newTable(pos);
    list.kind = .list;
    for (toks) |tk| {
        if (only) |o| if (tk.tag != o) continue;
        const m = try run.newTable(pos);
        m.kind = .map;
        try put(run, pos, m, "tag", .{ .string = @tagName(tk.tag) });
        try put(run, pos, m, "text", .{ .string = try keep(run, pos, src[tk.start..tk.end]) });
        try put(run, pos, m, "line", .{ .int = @intCast(za.lineOf(starts, tk.start)) });
        list.list.append(run.alloc, .{ .table = m }) catch return run.oom(pos);
    }
    return .{ .table = list };
}

/// The extra rewrites of a move with `from_as`, each with its spans against the
/// text before it: `done.to_now` to `to_qualified` to `to_imported`, and
/// `done.from_now` to `from_pub`.
const FromAs = struct {
    to_qualified: []const u8,
    qualified_spans: []const Span,
    to_imported: []const u8,
    import_spans: []const Span,
    from_pub: []const u8,
    pub_spans: []const Span,
    qualified: usize,
    made_pub: []const []const u8,
};

fn fromAs(sa: std.mem.Allocator, run: *Run, pos: ast.Pos, done: anytype, fa: []const u8, from_base: []const u8) Error!FromAs {
    const to_z = sa.dupeSentinel(u8, done.to_now, 0) catch return run.oom(pos);
    const toks = switch (za.tokens(sa, to_z) catch return run.oom(pos)) {
        .ok => |ok| ok,
        .invalid => |n| return run.fail(pos, .argument, "`zig_move`: the moved code does not tokenize at line {d}", .{n}),
    };
    for (toks, 0..) |tk, i| if (tk.tag == .identifier and std.mem.eql(u8, to_z[tk.start..tk.end], fa) and (i == 0 or toks[i - 1].tag != .period))
        return run.fail(pos, .argument, "`zig_move`: `{s}` is a name in the moved code already: `from_as` would shadow it; nothing written", .{fa});
    // Qualify the moved code's uses of `from`'s names.
    var renames: std.ArrayList(Rename) = .empty;
    var wanted: std.StringHashMapUnmanaged(void) = .empty;
    for (done.needs_from_from) |n| if (!wanted.contains(n.name)) {
        wanted.put(sa, n.name, {}) catch return run.oom(pos);
        renames.append(sa, .{ .from = n.name, .to = std.fmt.allocPrint(sa, "{s}.{s}", .{ fa, n.name }) catch return run.oom(pos) }) catch return run.oom(pos);
    };
    const q = switch (planMany(sa, to_z, renames.items, null) catch return run.oom(pos)) {
        .done => |d| d,
        .declaration => |d| return run.fail(pos, .argument, "`zig_move`: the moved code declares `{s}` (line {d}), which `from_as` would qualify; nothing written", .{ d.name, d.line }),
        .invalid => |n| return run.fail(pos, .argument, "`zig_move`: the moved code does not tokenize at line {d}", .{n}),
    };
    try run.charge(pos, q.tokens);
    // Insert the import after `to`'s last `@import`, or first if there is none.
    const qz = sa.dupeSentinel(u8, q.now, 0) catch return run.oom(pos);
    const line = std.fmt.allocPrint(sa, "const {s} = @import(\"{s}\");", .{ fa, from_base }) catch return run.oom(pos);
    var at: usize = 0;
    var text: []const u8 = undefined;
    switch (za.decls(sa, qz) catch return run.oom(pos)) {
        .ok => |ok| for (ok.decls) |d| if (std.mem.startsWith(u8, d.value, "@import(")) {
            at = d.stop;
        },
        .parse_error => |e| return run.fail(pos, .argument, "`zig_move`: the moved code does not parse at line {d}", .{e.line}),
    }
    if (at == 0) {
        text = std.fmt.allocPrint(sa, "{s}\n\n", .{line}) catch return run.oom(pos);
    } else text = std.fmt.allocPrint(sa, "\n{s}", .{line}) catch return run.oom(pos);
    const imported = std.mem.concat(sa, u8, &.{ q.now[0..at], text, q.now[at..] }) catch return run.oom(pos);
    const import_spans = sa.alloc(Span, 1) catch return run.oom(pos);
    import_spans[0] = .{ .o0 = at, .o1 = at, .c0 = at, .c1 = at + text.len };
    // Make those names `pub` in `from`, inserting past any doc comment.
    const fz = sa.dupeSentinel(u8, done.from_now, 0) catch return run.oom(pos);
    var made: std.ArrayList([]const u8) = .empty;
    var offsets: std.ArrayList(usize) = .empty;
    switch (za.decls(sa, fz) catch return run.oom(pos)) {
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
            offsets.append(sa, k) catch return run.oom(pos);
            made.append(sa, d.name) catch return run.oom(pos);
        },
        .parse_error => |e| return run.fail(pos, .argument, "`zig_move`: `from`, cut, does not parse at line {d}", .{e.line}),
    }
    var out: std.ArrayList(u8) = .empty;
    var spans: std.ArrayList(Span) = .empty;
    var copied: usize = 0;
    for (offsets.items) |o| {
        out.appendSlice(sa, fz[copied..o]) catch return run.oom(pos);
        const c0 = out.items.len;
        out.appendSlice(sa, "pub ") catch return run.oom(pos);
        spans.append(sa, .{ .o0 = o, .o1 = o, .c0 = c0, .c1 = out.items.len }) catch return run.oom(pos);
        copied = o;
    }
    out.appendSlice(sa, fz[copied..]) catch return run.oom(pos);
    std.mem.sort([]const u8, made.items, {}, struct {
        fn lt(_: void, x: []const u8, y: []const u8) bool {
            return std.mem.lessThan(u8, x, y);
        }
    }.lt);
    return .{ .to_qualified = q.now, .qualified_spans = q.spans, .to_imported = imported, .import_spans = import_spans, .from_pub = out.items, .pub_spans = spans.items, .qualified = q.renamed, .made_pub = made.items };
}

/// A Zig file as read (`was`, in the run's arena when it may be handed to core)
/// and NUL-terminated for the parser (`src`, in the scratch).
const Read = struct { was: []const u8, src: [:0]const u8 };
/// Reads `p`, charging the read and the copy. Returns `null` for a missing file
/// when `missing_ok`.
fn readZig(sa: std.mem.Allocator, run: *Run, pos: ast.Pos, p: script.builtins_store.Path, missing_ok: bool, kept: bool) Error!?Read {
    const hub = script.builtins_store;
    try hub.countRead(run, pos);
    if (missing_ok) {
        const st = webbox.store.statOrNull(run.io, run.alloc, p.full()) catch |e| return hub.storeFailed(run, pos, .host, p.rel(), e);
        if (st == null) return null;
    }
    const size = try script.builtins_read.fileSize(run, pos, .host, p);
    const was = try script.builtins_read.readSizedIn(if (kept) run.alloc else sa, run, pos, .host, p, size, 0, null);
    const src = sa.dupeSentinel(u8, was, 0) catch return run.oom(pos);
    try script.builtins.chargeBytes(run, pos, src.len);
    return .{ .was = was, .src = src };
}

fn put(run: *Run, pos: ast.Pos, t: *script.value.Table, k: []const u8, v: Value) Error!void {
    t.map.put(run.alloc, k, v) catch return run.oom(pos);
}

/// A list of `{name, line}` maps, or `{name, target, line}` if items have a target.
fn nameLines(run: *Run, pos: ast.Pos, items: anytype) Error!Value {
    const list = try run.newTable(pos);
    list.kind = .list;
    for (items) |x| {
        const m = try run.newTable(pos);
        m.kind = .map;
        try put(run, pos, m, "name", .{ .string = try keep(run, pos, x.name) });
        if (@hasField(@TypeOf(x), "target")) try put(run, pos, m, "target", .{ .string = try keep(run, pos, x.target) });
        try put(run, pos, m, "line", .{ .int = @intCast(x.line) });
        list.list.append(run.alloc, .{ .table = m }) catch return run.oom(pos);
    }
    return .{ .table = list };
}

/// `host("zig_move", from, to, {names, opts})`: moves top-level declarations
/// from one Zig file to another (`shale_zig_move.zig`) and reports what the
/// move broke. A missing `to` is created. `to` is written before `from`, so a
/// stop between them leaves the declarations in both, never in neither.
/// Charges the reads, the copies, and a step per token and node of both files.
pub fn move(backing: std.mem.Allocator, run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const zm = @import("shale_zig_move.zig");
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
    var wants: std.ArrayList(zm.Want) = .empty;
    for (names.list.items) |v| switch (v) {
        .string => |s| wants.append(a, .{ .name = s }) catch return run.oom(pos),
        .table => |t| {
            if (t.kind != .map) return run.fail(pos, .type, "`zig_move`: a name is a string, or `{{kind = ..., name = ...}}`", .{});
            var w: zm.Want = .{ .name = "" };
            for (t.map.keys(), t.map.values()) |k, x| {
                const s = switch (x) {
                    .string => |s| s,
                    else => return run.fail(pos, .type, "`zig_move`: a name's `{s}` is a string", .{k}),
                };
                if (std.mem.eql(u8, k, "name")) {
                    w.name = s;
                } else if (std.mem.eql(u8, k, "kind")) {
                    w.kind = std.meta.stringToEnum(za.Kind, s) orelse return run.fail(pos, .argument, "`zig_move`: no kind `{s}`: fn, const, var, test, comptime or field", .{s});
                } else return run.fail(pos, .argument, "`zig_move`: a name takes `kind` and `name`, not `{s}`", .{k});
            }
            wants.append(a, w) catch return run.oom(pos);
        },
        else => return run.fail(pos, .type, "`zig_move`: a name is a string, or `{{kind = ..., name = ...}}`", .{}),
    };
    if (wants.items.len == 0) return run.fail(pos, .argument, "`zig_move`: no names: nothing to move", .{});
    var mode: zm.CommentMode = .doc;
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
                mode = std.meta.stringToEnum(zm.CommentMode, s) orelse return run.fail(pos, .argument, "`zig_move`'s `comment` is \"doc\" or \"doc_and_plain\", not \"{s}\"", .{s});
            } else if (std.mem.eql(u8, k, "after")) {
                after = s;
            } else if (std.mem.eql(u8, k, "from_as")) {
                if (!std.zig.isValidId(s)) return run.fail(pos, .argument, "`zig_move`'s `from_as` (\"{s}\") is not a Zig identifier", .{s});
                from_as = s;
            } else return run.fail(pos, .argument, "`zig_move` takes `comment`, `after` and `from_as`, not `{s}`", .{k});
        },
        else => |v| return run.fail(pos, .type, "`zig_move`'s options are a map, not {s}", .{v.typeName()}),
    }
    var scratch: Scratch = undefined;
    const sa = scratch.init(backing, run);
    defer scratch.deinit();
    const from_read = (try readZig(sa, run, pos, p, false, true)).?;
    const to_read: Read = (try readZig(sa, run, pos, q, true, true)) orelse .{ .was = "", .src = "" };
    const from_src = from_read.was;
    const to_src = to_read.was;
    const done = switch (zm.move(sa, from_read.src, to_read.src, wants.items, mode, after) catch return run.oom(pos)) {
        .done => |d| d,
        .refused => |why| return run.fail(pos, .argument, "`zig_move` {s} to {s}: {s}", .{ p.rel(), q.rel(), try keep(run, pos, why) }),
    };
    try run.charge(pos, done.work);
    // Check both files may change before writing either.
    try script.builtins_change.mayChange(run, pos, q);
    try script.builtins_change.mayChange(run, pos, p);
    const spans = struct {
        fn of(r: *Run, ps: ast.Pos, xs: []const zm.Span) Error![]const Span {
            const out = r.alloc.alloc(Span, xs.len) catch return r.oom(ps);
            for (xs, out) |x, *o| o.* = .{ .o0 = x.o0, .o1 = x.o1, .c0 = x.c0, .c1 = x.c1 };
            return out;
        }
    }.of;
    // `from_as` rewrites are planned before anything is written.
    var reach: ?FromAs = null;
    if (from_as) |fa| reach = try fromAs(sa, run, pos, done, fa, std.fs.path.basename(p.rel()));
    const writes: u64 = if (reach != null) 5 else 2;
    // Check the budgets for every write up front, so a budget never stops a
    // move halfway.
    var bytes: u64 = done.to_now.len + done.from_now.len;
    if (reach) |r| bytes += r.to_qualified.len + r.to_imported.len + r.from_pub.len;
    if (run.changes + writes > run.budgets.changes) return run.budgetStop(pos, .changes);
    if (bytes > run.budgets.change_bytes -| run.change_bytes) return run.budgetStop(pos, .change_bytes);
    const to_spans = try spans(run, pos, done.to_spans);
    const from_spans = try spans(run, pos, done.from_spans);
    try script.builtins_change.rewrite(run, pos, q, to_src, try keep(run, pos, done.to_now), to_spans);
    // The store or the output budget can still stop the second write; the
    // message then says `to` already holds the copies.
    script.builtins_change.rewrite(run, pos, p, from_src, try keep(run, pos, done.from_now), from_spans) catch |e| {
        if (e == error.Stop) if (run.stop) |*s| {
            s.message = std.fmt.allocPrint(a, "{s}; `zig_move` wrote {s} first, so it holds the moved declarations, which {s} still has: take {s} back (`git checkout --`) before moving again", .{ s.message, q.rel(), p.rel(), q.rel() }) catch s.message;
        };
        return e;
    };
    if (reach) |r| {
        try script.builtins_change.rewrite(run, pos, q, try keep(run, pos, done.to_now), try keep(run, pos, r.to_qualified), a.dupe(Span, r.qualified_spans) catch return run.oom(pos));
        try script.builtins_change.rewrite(run, pos, q, try keep(run, pos, r.to_qualified), try keep(run, pos, r.to_imported), a.dupe(Span, r.import_spans) catch return run.oom(pos));
        try script.builtins_change.rewrite(run, pos, p, try keep(run, pos, done.from_now), try keep(run, pos, r.from_pub), a.dupe(Span, r.pub_spans) catch return run.oom(pos));
    }
    const t = try run.newTable(pos);
    t.kind = .map;
    try put(run, pos, t, "moved", .{ .int = @intCast(done.moved) });
    if (reach) |r| {
        try put(run, pos, t, "qualified", .{ .int = @intCast(r.qualified) });
        const mp = try run.newTable(pos);
        mp.kind = .list;
        for (r.made_pub) |n| mp.list.append(a, .{ .string = try keep(run, pos, n) }) catch return run.oom(pos);
        try put(run, pos, t, "made_pub", .{ .table = mp });
    }
    try put(run, pos, t, "needs_pub", try nameLines(run, pos, done.needs_pub));
    try put(run, pos, t, "needs_import", try nameLines(run, pos, done.needs_import));
    try put(run, pos, t, "needs_from_from", try nameLines(run, pos, done.needs_from_from));
    try put(run, pos, t, "uses_left", try nameLines(run, pos, done.uses_left));
    return .{ .table = t };
}

/// `host("zig_imports", path)`: a Zig file's `@import`s and `@embedFile`s in
/// source order, from webbox's `zig_refs.zig`, as `{kind, target, name, line,
/// file}`. `file` is `found` or `missing` beside the importer, or `module` for a
/// target with no extension. Charges the read, the copy, a step per token and
/// node, and a step per call for the file check.
pub fn imports(backing: std.mem.Allocator, run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    if (args.len != 1) return run.fail(pos, .argument, "`zig_imports` takes a path, given {d} arguments", .{args.len});
    const a = run.alloc;
    const p = try script.builtins_store.pathArg(run, pos, .host, args[0], false);
    var scratch: Scratch = undefined;
    const sa = scratch.init(backing, run);
    defer scratch.deinit();
    const src = (try readZig(sa, run, pos, p, false, false)).?.src;
    const got = switch (webbox.zig_refs.refs(sa, src) catch return run.oom(pos)) {
        .ok => |ok| ok,
        .parse_error => |line| return run.fail(pos, .argument, "`zig_imports`: {s} does not parse as Zig: line {d}", .{ p.rel(), line }),
    };
    try run.charge(pos, got.tokens + got.nodes + got.refs.len);
    const dir = std.fs.path.dirnamePosix(p.full()) orelse ".";
    const list = try run.newTable(pos);
    list.kind = .list;
    for (got.refs) |r| {
        const file: []const u8 = if (std.fs.path.extension(r.target).len == 0) "module" else file: {
            const full = std.fs.path.resolvePosix(sa, &.{ dir, r.target }) catch return run.oom(pos);
            const st = webbox.store.statOrNull(run.io, sa, full) catch |e| return script.builtins_store.storeFailed(run, pos, .host, r.target, e);
            break :file if (st != null) "found" else "missing";
        };
        const m = try run.newTable(pos);
        m.kind = .map;
        try put(run, pos, m, "kind", .{ .string = @tagName(r.kind) });
        try put(run, pos, m, "target", .{ .string = try keep(run, pos, r.target) });
        try put(run, pos, m, "name", .{ .string = try keep(run, pos, r.name) });
        try put(run, pos, m, "line", .{ .int = @intCast(r.line) });
        try put(run, pos, m, "file", .{ .string = file });
        list.list.append(a, .{ .table = m }) catch return run.oom(pos);
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
