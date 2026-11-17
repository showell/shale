//! Plans `zig_rename`: every identifier token `from` becomes `to`, but never one
//! right after `.` (a field, a member, an enum literal), never `@"from"`, and
//! never in a string, character or comment. The analysis is tokens, not types;
//! the compiler judges after. A qualified `to` (with a `.`) where `from` is
//! declared stops the plan, since `pub const m.from` is not Zig. Pure: source
//! in, the new text and its spans out; Shale's host writes it.

const std = @import("std");
const za = @import("zig_ast.zig");
const zm = @import("zig_move.zig");

pub const Span = zm.Span;

/// Lines, 1-based, inclusive.
pub const Lines = za.Lines;

/// A planned rename: the new text, the changed spans, the count renamed, the
/// whole-word matches left in comments (plain and doc), and the tokens read.
pub const Plan = struct { now: []const u8, spans: []const Span, renamed: usize, left_in_comments: usize, tokens: usize };

/// A plan, or where planning stopped: `declaration` is a qualified `to` where
/// `from` is declared; `invalid` is the line of a token the tokenizer refuses.
pub const Outcome = union(enum) { done: Plan, declaration: struct { line: usize, name: []const u8 }, invalid: usize };

/// One name and what it becomes.
pub const Rename = struct { from: []const u8, to: []const u8 };

/// Plans renaming `from` to `to` in `src`, within `lines` if given. Reads
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
/// naming its line, rather than be guessed at.
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
/// after a `.`; every other `.from` is left, its line said.
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
