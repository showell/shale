//! A linear-time regular expression engine, behind `re_find` and `re_match`.
//! Supports literals, `.`, classes (`[a-z]`, `[^...]`, `\d \w \s` and their
//! negations), `*`, `+`, `?`, `^`, `$`, alternation (lowest precedence,
//! leftmost preferred), `\b \B` (ASCII word edges), capture groups (up to
//! nine, nested or not, numbered by their `(` left to right) and groups that
//! do not capture (`(?:...)`);
//! `nocase` folds ASCII letters at compile time. Counted repetition,
//! backreferences and lookaround are refused with a message.
//!
//! A loop whose turn can match nothing (`(\w*(|.))*`, `(a*)*`) ends at a turn
//! that took nothing, as a backtracker's does: its threads carry a bit for
//! it, so a state is an instruction and those bits (at most four loops deep,
//! sixteen states an instruction), and the match is the backtracker's, groups
//! and all (QUEUE 253 found where it was not; 259 made it so).
//!
//! A pattern compiles to a Pike VM program of a few instructions per byte. A
//! search advances all threads one byte at a time in order of preference,
//! visiting each instruction at most once per position, so it finds the match
//! a backtracking matcher would find first in at most (string bytes + 1) times
//! the program's instructions, whatever the pattern. Pure: no I/O, and all
//! memory is the caller's.

const std = @import("std");

/// The longest pattern accepted; a longer one is refused before it is read.
pub const pattern_most = 256;

/// The most capture groups a pattern may have: `\1` to `\9` in a replacement.
pub const groups_most = 9;

pub const Op = enum(u8) {
    /// The byte `c`.
    byte,
    /// Any byte but a newline.
    any,
    /// A byte in `sets[x]`.
    set,
    /// Continue at `x`, and, less preferred, at `y`.
    split,
    /// Continue at `x`.
    jmp,
    /// Record the position in capture slot `x` (`2k` group k+1's start, `2k+1`
    /// its end).
    save,
    /// Matches only at the start of the string.
    start,
    /// Matches only at the end of the string.
    end,
    /// Matches only at a word edge (`c` 0) or only not at one (`c` 1). An edge
    /// has a word byte on one side and none on the other.
    boundary,
    /// A turn of loop `x` begins: its "took something" bit is cleared.
    turn,
    /// A turn of loop `x` ends: one that took nothing ends the loop here, as
    /// a backtracker's empty turn does.
    again,
    /// A `+`'s first turn ends: one that took nothing goes on to `y`, past the
    /// loop, as a backtracker's does; one that took something, on.
    first,
    /// The pattern has matched.
    match,
};

pub const Inst = struct { op: Op, c: u8 = 0, x: u16 = 0, y: u16 = 0 };

pub const Set = std.StaticBitSet(256);

pub const Program = struct {
    insts: []Inst,
    sets: []Set,
    /// How many capture groups the pattern has.
    groups: u8,
    /// Whether the pattern begins with `^`, so it can only match at the start.
    anchored: bool,
    /// Each instruction's loops whose turn can take nothing, the ones its
    /// thread's bits are told apart by.
    encl: []Encl,
    /// Where each instruction's states start: one for each setting of its
    /// `encl` bits.
    base: []u32,
    /// The states a position can hold: the instructions, each times two to
    /// the loops around it whose turn can take nothing.
    states: u32,

    pub fn deinit(p: *Program, alloc: std.mem.Allocator) void {
        alloc.free(p.insts);
        alloc.free(p.sets);
        alloc.free(p.encl);
        alloc.free(p.base);
    }
};

/// The loops (by number) whose turn can take nothing around one instruction.
pub const Encl = struct { n: u8 = 0, ids: [nest_most]u8 = undefined };

/// Loops whose turn can take nothing: at most this many a pattern, nested at
/// most `nest_most` deep, so a position's states are at most sixteen times
/// the instructions.
pub const empty_loops_most = 16;
pub const nest_most = 4;

/// Why a pattern was refused: the offending byte offset, and a message saying
/// what to write instead.
pub const Refusal = struct { at: usize, message: []const u8 };

pub const Error = error{ OutOfMemory, Refused };

pub const Options = struct {
    /// ASCII letters match either case, folded at compile time. A class is
    /// folded before its `^` is applied (`[^a]` matches neither `a` nor `A`),
    /// as in Python.
    nocase: bool = false,
};

const Quant = enum { one, star, plus, opt };

/// A pattern's alternatives, each a sequence of nodes.
const Alt = []const []const Node;

const Node = struct {
    kind: enum { byte, any, set, start, end, boundary, not_boundary, group },
    c: u8 = 0,
    set: u16 = 0,
    quant: Quant = .one,
    /// The group's alternatives.
    inner: Alt = &.{},
    /// The group's number, from 1; 0 for one that does not capture.
    index: u8 = 0,
};

const Reader = struct {
    p: []const u8,
    at: usize = 0,
    /// Allocates the sets, which the program keeps.
    alloc: std.mem.Allocator,
    /// Allocates the tree, freed once the program is emitted.
    tree: std.mem.Allocator,
    refused: *Refusal,
    options: Options,
    sets: std.ArrayList(Set) = .empty,
    groups: u8 = 0,

    fn refuse(r: *Reader, at: usize, message: []const u8) Error {
        r.refused.* = .{ .at = at, .message = message };
        return error.Refused;
    }

    /// Parses alternatives up to the end of the pattern or, in the group, its `)`.
    fn alt(r: *Reader, in_group: bool) Error!Alt {
        var seqs: std.ArrayList([]const Node) = .empty;
        while (true) {
            try seqs.append(r.tree, try r.seq(in_group));
            if (r.at < r.p.len and r.p[r.at] == '|') {
                r.at += 1;
                continue;
            }
            return seqs.items;
        }
    }

    /// Parses a sequence up to the end, a `|`, or, in the group, its `)`.
    fn seq(r: *Reader, in_group: bool) Error![]const Node {
        var list: std.ArrayList(Node) = .empty;
        while (r.at < r.p.len) {
            const at = r.at;
            const c = r.p[at];
            var node: Node = switch (c) {
                ')' => {
                    if (in_group) return list.items;
                    return r.refuse(at, "a `)` with no `(` before it: `\\)` is the byte");
                },
                '|' => return list.items,
                '(' => blk: {
                    var index: u8 = 0;
                    if (r.at + 1 < r.p.len and r.p[r.at + 1] == '?') {
                        if (r.at + 2 >= r.p.len or r.p[r.at + 2] != ':') return r.refuse(at, "`(?` is not in this subset but for `(?:...)`, a group that does not capture: no lookaround, no named groups");
                        r.at += 3;
                    } else {
                        if (r.groups == groups_most) return r.refuse(at, "a tenth capture group: nine is the most (`\\9` is the last), so write this one `(?:...)` if it need not capture");
                        r.groups += 1;
                        index = r.groups;
                        r.at += 1;
                    }
                    const inner = try r.alt(true);
                    if (r.at >= r.p.len) return r.refuse(at, "a `(` that is never closed");
                    r.at += 1; // )
                    break :blk .{ .kind = .group, .inner = inner, .index = index };
                },
                '{' => return r.refuse(at, "`{n,m}` is not in this subset: write the part out as often as it must come (`\\d\\d\\d?`), or `\\{` for the byte"),
                '*', '+', '?' => return r.refuse(at, "a `*`, `+` or `?` with nothing before it to repeat"),
                '.' => blk: {
                    r.at += 1;
                    break :blk .{ .kind = .any };
                },
                '^' => blk: {
                    r.at += 1;
                    break :blk .{ .kind = .start };
                },
                '$' => blk: {
                    r.at += 1;
                    break :blk .{ .kind = .end };
                },
                '[' => try r.class(),
                '\\' => try r.escape(),
                else => blk: {
                    r.at += 1;
                    break :blk try r.byteNode(c);
                },
            };
            if (r.at < r.p.len) switch (r.p[r.at]) {
                '*', '+', '?' => |q| {
                    if (node.kind == .start or node.kind == .end or node.kind == .boundary or node.kind == .not_boundary)
                        return r.refuse(r.at, "`^`, `$`, `\\b` and `\\B` are places, not bytes: they cannot repeat");
                    node.quant = switch (q) {
                        '*' => .star,
                        '+' => .plus,
                        else => .opt,
                    };
                    r.at += 1;
                    if (r.at < r.p.len) switch (r.p[r.at]) {
                        '?' => return r.refuse(r.at, "lazy repetition (`*?`, `+?`, `??`) is not in this subset: every repetition takes as much as it can"),
                        '*', '+' => return r.refuse(r.at, "a repetition repeated (`a**`, `a+*`): one is enough"),
                        '{' => return r.refuse(r.at, "`{n,m}` is not in this subset: write the part out as often as it must come (`\\d\\d\\d?`), or `\\{` for the byte"),
                        else => {},
                    };
                },
                '{' => return r.refuse(r.at, "`{n,m}` is not in this subset: write the part out as often as it must come (`\\d\\d\\d?`), or `\\{` for the byte"),
                else => {},
            };
            try list.append(r.tree, node);
        }
        return list.items;
    }

    /// A byte node, or under `nocase` a set of a letter's two cases.
    fn byteNode(r: *Reader, c: u8) Error!Node {
        if (r.options.nocase and std.ascii.isAlphabetic(c)) {
            var s = Set.empty;
            s.set(std.ascii.toLower(c));
            s.set(std.ascii.toUpper(c));
            return r.newSet(s);
        }
        return .{ .kind = .byte, .c = c };
    }

    fn newSet(r: *Reader, s: Set) Error!Node {
        try r.sets.append(r.alloc, s);
        return .{ .kind = .set, .set = @intCast(r.sets.items.len - 1) };
    }

    /// Returns the set for `\d \w \s` or their negations; null for any other.
    fn shorthand(c: u8) ?Set {
        var s = Set.empty;
        switch (std.ascii.toLower(c)) {
            'd' => s.setRangeValue(.{ .start = '0', .end = '9' + 1 }, true),
            'w' => {
                s.setRangeValue(.{ .start = '0', .end = '9' + 1 }, true);
                s.setRangeValue(.{ .start = 'a', .end = 'z' + 1 }, true);
                s.setRangeValue(.{ .start = 'A', .end = 'Z' + 1 }, true);
                s.set('_');
            },
            's' => for (" \t\n\r\x0b\x0c") |b| s.set(b),
            else => return null,
        }
        if (std.ascii.isUpper(c)) s.toggleAll();
        return s;
    }

    /// Returns the byte a single-byte escape stands for: `\n \t \r`, or any
    /// byte that is not a letter or digit, itself.
    fn escapedByte(r: *Reader, at: usize, c: u8) Error!u8 {
        switch (c) {
            'n' => return '\n',
            't' => return '\t',
            'r' => return '\r',
            '0'...'9' => return r.refuse(at, "backreferences (`\\1`) are not in this subset"),
            else => {},
        }
        if (std.ascii.isAlphabetic(c)) return r.refuse(at, "an escape this subset does not have: `\\d \\w \\s` (and `\\D \\W \\S`), `\\b \\B`, `\\n \\t \\r`, or `\\` before a byte that is not a letter or digit");
        return c;
    }

    fn escape(r: *Reader) Error!Node {
        const at = r.at;
        if (at + 1 >= r.p.len) return r.refuse(at, "a pattern that ends in `\\`: `\\\\` is the byte");
        const c = r.p[at + 1];
        r.at += 2;
        if (shorthand(c)) |s| return r.newSet(s);
        if (c == 'b') return .{ .kind = .boundary };
        if (c == 'B') return .{ .kind = .not_boundary };
        return r.byteNode(try r.escapedByte(at, c));
    }

    /// Parses `[...]`: bytes, ranges (`a-z`) and shorthands. A leading `^`
    /// negates it; a `]` first is a literal. Under `nocase`, letters are
    /// folded before negation.
    fn class(r: *Reader) Error!Node {
        const open = r.at;
        r.at += 1;
        var s = Set.empty;
        const negated = r.at < r.p.len and r.p[r.at] == '^';
        if (negated) r.at += 1;
        var first = true;
        while (true) : (first = false) {
            if (r.at >= r.p.len) return r.refuse(open, "a `[` that is never closed");
            const at = r.at;
            const c = r.p[at];
            if (c == ']' and !first) {
                r.at += 1;
                break;
            }
            if (c == '[' and at + 1 < r.p.len and r.p[at + 1] == ':') return r.refuse(at, "`[:alpha:]` and its kind are not in this subset: write the bytes (`[a-zA-Z]`), or `\\[` for the byte");
            var lo: u8 = c;
            if (c == '\\') {
                if (at + 1 >= r.p.len) return r.refuse(open, "a `[` that is never closed");
                const e = r.p[at + 1];
                r.at += 2;
                if (shorthand(e)) |sh| {
                    s.setUnion(sh);
                    continue;
                }
                if (e == 'b' or e == 'B') return r.refuse(at, "`\\b` in a class is not in this subset: a word's edge is a place, not a byte; write it outside the `[...]`");
                lo = try r.escapedByte(at, e);
            } else r.at += 1;
            // A `-` just before `]` is a literal, not a range.
            if (r.at + 1 < r.p.len and r.p[r.at] == '-' and r.p[r.at + 1] != ']') {
                const hi_at = r.at + 1;
                var hi = r.p[hi_at];
                r.at += 2;
                if (hi == '\\') {
                    if (hi_at + 1 >= r.p.len) return r.refuse(open, "a `[` that is never closed");
                    const e = r.p[hi_at + 1];
                    if (shorthand(e) != null) return r.refuse(hi_at, "a range's end must be one byte, not a class");
                    hi = try r.escapedByte(hi_at, e);
                    r.at += 1;
                }
                if (hi < lo) return r.refuse(at, "a range whose end is before its start (`z-a`): write it `a-z`");
                s.setRangeValue(.{ .start = lo, .end = @as(usize, hi) + 1 }, true);
            } else s.set(lo);
        }
        if (r.options.nocase) {
            var folded = s;
            var it = s.iterator(.{});
            while (it.next()) |b| if (std.ascii.isAlphabetic(@intCast(b))) {
                folded.set(std.ascii.toLower(@intCast(b)));
                folded.set(std.ascii.toUpper(@intCast(b)));
            };
            s = folded;
        }
        if (negated) s.toggleAll();
        return r.newSet(s);
    }
};

/// Whether `n` can match without taking a byte.
fn nullable(n: Node) bool {
    if (n.quant == .star or n.quant == .opt) return true;
    return switch (n.kind) {
        .byte, .any, .set => false,
        .start, .end, .boundary, .not_boundary => true,
        .group => nullableAlt(n.inner),
    };
}

fn nullableAlt(a: Alt) bool {
    for (a) |s| {
        var all = true;
        for (s) |n| if (!nullable(n)) {
            all = false;
            break;
        };
        if (all) return true;
    }
    return false;
}

/// A loop's turn, instructions `from` to `to`: the instructions whose
/// threads loop `id`'s bit tells apart.
const Range = struct { id: u8, from: u16, to: u16 };

const Emitter = struct {
    insts: std.ArrayList(Inst) = .empty,
    alloc: std.mem.Allocator,
    refused: *Refusal,
    /// Loops whose turn can take nothing, numbered as they are emitted.
    loops: u8 = 0,
    ranges: std.ArrayList(Range) = .empty,

    /// A new loop whose turn can take nothing.
    fn emptyLoop(e: *Emitter) Error!u8 {
        if (e.loops == empty_loops_most) {
            e.refused.* = .{ .at = 0, .message = "more than 16 loops whose turn can match nothing (`(a*)*`, `(a|)+`): write some so a turn takes a byte" };
            return error.Refused;
        }
        e.loops += 1;
        return e.loops - 1;
    }

    /// One turn of loop `id`, `turn`, the atom, and its end (`op`, `again`
    /// or `first`), recorded as a range: the end's index.
    fn turnOf(e: *Emitter, n: Node, id: u8, op: Op) Error!u16 {
        const from = try e.emit(.{ .op = .turn, .x = id });
        try e.atom(n);
        const end = try e.emit(.{ .op = op, .x = id });
        try e.ranges.append(e.alloc, .{ .id = id, .from = from, .to = end });
        return end;
    }

    fn emit(e: *Emitter, i: Inst) Error!u16 {
        try e.insts.append(e.alloc, i);
        return @intCast(e.insts.items.len - 1);
    }

    fn here(e: *const Emitter) u16 {
        return @intCast(e.insts.items.len);
    }

    /// Emits a split before each alternative but the last, the leftmost
    /// preferred, each ending in a jump past the rest.
    fn alt(e: *Emitter, a: Alt) Error!void {
        var ends: [pattern_most + 1]u16 = undefined;
        var n_ends: usize = 0;
        for (a, 0..) |s, k| {
            if (k + 1 == a.len) {
                try e.seq(s);
                break;
            }
            const split = try e.emit(.{ .op = .split });
            e.insts.items[split].x = split + 1;
            try e.seq(s);
            ends[n_ends] = try e.emit(.{ .op = .jmp });
            n_ends += 1;
            e.insts.items[split].y = e.here();
        }
        for (ends[0..n_ends]) |j| e.insts.items[j].x = e.here();
    }

    fn seq(e: *Emitter, nodes: []const Node) Error!void {
        for (nodes) |n| try e.node(n);
    }

    fn atom(e: *Emitter, n: Node) Error!void {
        switch (n.kind) {
            .byte => _ = try e.emit(.{ .op = .byte, .c = n.c }),
            .any => _ = try e.emit(.{ .op = .any }),
            .set => _ = try e.emit(.{ .op = .set, .x = n.set }),
            .start => _ = try e.emit(.{ .op = .start }),
            .end => _ = try e.emit(.{ .op = .end }),
            .boundary => _ = try e.emit(.{ .op = .boundary, .c = 0 }),
            .not_boundary => _ = try e.emit(.{ .op = .boundary, .c = 1 }),
            .group => {
                if (n.index == 0) return e.alt(n.inner);
                const k: u16 = n.index - 1;
                _ = try e.emit(.{ .op = .save, .x = 2 * k });
                try e.alt(n.inner);
                _ = try e.emit(.{ .op = .save, .x = 2 * k + 1 });
            },
        }
    }

    /// Thompson's construction. Each split prefers its first way, so every
    /// repetition is greedy.
    fn node(e: *Emitter, n: Node) Error!void {
        // **A LOOP WHOSE TURN CAN TAKE NOTHING** (QUEUE 259): its threads
        // carry a bit, "this turn took something", and states are told
        // apart by it, so a new turn begun where an earlier one stood is not
        // lost to that one's visit. A turn that took nothing ends the loop
        // (`again`); a `+`'s first such turn goes on past it (`first`), as a
        // backtracker's do.
        if ((n.quant == .star or n.quant == .plus) and nullable(.{ .kind = n.kind, .c = n.c, .set = n.set, .inner = n.inner, .index = n.index })) {
            const id = try e.emptyLoop();
            var first: ?u16 = null;
            if (n.quant == .plus) first = try e.turnOf(n, id, .first);
            const loop = try e.emit(.{ .op = .split });
            _ = try e.turnOf(n, id, .again);
            _ = try e.emit(.{ .op = .jmp, .x = loop });
            e.insts.items[loop].x = loop + 1;
            e.insts.items[loop].y = e.here();
            if (first) |f| e.insts.items[f].y = e.here();
            return;
        }
        switch (n.quant) {
            .one => try e.atom(n),
            .star => {
                const loop = try e.emit(.{ .op = .split });
                try e.atom(n);
                _ = try e.emit(.{ .op = .jmp, .x = loop });
                e.insts.items[loop].x = loop + 1;
                e.insts.items[loop].y = e.here();
            },
            .plus => {
                const top = e.here();
                try e.atom(n);
                const s = try e.emit(.{ .op = .split, .x = top });
                e.insts.items[s].y = s + 1;
            },
            .opt => {
                const s = try e.emit(.{ .op = .split });
                try e.atom(n);
                e.insts.items[s].x = s + 1;
                e.insts.items[s].y = e.here();
            },
        }
    }
};

/// Compiles a pattern, or refuses it with `refused` set to where and why.
/// The program is allocated from `alloc` and freed by `Program.deinit`.
pub fn compile(alloc: std.mem.Allocator, pattern: []const u8, options: Options, refused: *Refusal) Error!Program {
    if (pattern.len > pattern_most) {
        refused.* = .{ .at = pattern_most, .message = "a pattern of more than 256 bytes" };
        return error.Refused;
    }
    var tree = std.heap.ArenaAllocator.init(alloc);
    defer tree.deinit();
    var r: Reader = .{ .p = pattern, .alloc = alloc, .tree = tree.allocator(), .refused = refused, .options = options };
    errdefer r.sets.deinit(alloc);
    const top = try r.alt(false);
    var e: Emitter = .{ .alloc = alloc, .refused = refused };
    errdefer e.insts.deinit(alloc);
    defer e.ranges.deinit(alloc);
    try e.alt(top);
    _ = try e.emit(.{ .op = .match });
    const insts = try e.insts.toOwnedSlice(alloc);
    errdefer alloc.free(insts);
    // Each instruction's loops, from the turns' ranges, and its states.
    const encl = try alloc.alloc(Encl, insts.len);
    errdefer alloc.free(encl);
    @memset(encl, .{});
    for (e.ranges.items) |rg| for (encl[rg.from .. rg.to + 1]) |*en| {
        if (std.mem.indexOfScalar(u8, en.ids[0..en.n], rg.id) != null) continue;
        if (en.n == nest_most) {
            refused.* = .{ .at = 0, .message = "loops whose turn can match nothing (`(a*)*`), nested more than four deep" };
            return error.Refused;
        }
        en.ids[en.n] = rg.id;
        en.n += 1;
    };
    const base = try alloc.alloc(u32, insts.len);
    errdefer alloc.free(base);
    var states: u32 = 0;
    for (encl, base) |en, *b| {
        b.* = states;
        states += @as(u32, 1) << @intCast(en.n);
    }
    return .{
        .encl = encl,
        .base = base,
        .states = states,
        .insts = insts,
        .sets = try r.sets.toOwnedSlice(alloc),
        .groups = r.groups,
        .anchored = top.len == 1 and top[0].len > 0 and top[0][0].kind == .start,
    };
}

/// A new thread's captures, its match begun at `pos`.
fn startCaps(pos: usize) Caps {
    var c: Caps = @splat(unset);
    c[0] = pos;
    return c;
}

/// The groups a match's captures recorded, start and end both.
fn groupsOf(caps: Caps) [groups_most]?[2]usize {
    var g: [groups_most]?[2]usize = @splat(null);
    for (&g, 0..) |*x, k| {
        if (caps[1 + 2 * k] != unset and caps[2 + 2 * k] != unset) x.* = .{ caps[1 + 2 * k], caps[2 + 2 * k] };
    }
    return g;
}

fn wordByte(c: u8) bool {
    return std.ascii.isAlphanumeric(c) or c == '_';
}

/// A thread's captures: the match's start, then each group's start and end,
/// each `unset` until recorded.
const Caps = [1 + 2 * groups_most]usize;
const unset = std.math.maxInt(usize);

/// `flags`: bit `k` set when loop `k`'s turn has taken a byte (every bit is
/// set by a byte taken; a turn clears its own).
const Thread = struct { pc: u16, caps: Caps, flags: u16 = 0 };

/// The state a thread is in: its instruction and the bits of the loops
/// around it.
fn stateOf(p: *const Program, pc: u16, flags: u16) u32 {
    const en = p.encl[pc];
    var k: u32 = 0;
    for (en.ids[0..en.n], 0..) |id, i| {
        if (flags & (@as(u16, 1) << @intCast(id)) != 0) k |= @as(u32, 1) << @intCast(i);
    }
    return p.base[pc] + k;
}

/// The threads at one position, in order of preference. `stamp[pc] == now`
/// marks an instruction already visited at this position; `reset` clears all
/// marks by advancing `now`.
const List = struct {
    threads: []Thread,
    stamp: []u64,
    now: u64 = 1,
    len: usize = 0,

    fn reset(l: *List) void {
        l.len = 0;
        l.now += 1;
    }
};

/// Working memory for searches, allocated once per program and reused.
pub const Scratch = struct {
    a: List,
    b: List,
    /// Pending split alternatives while threads are added: a split's second
    /// way waits while its first is followed.
    stack: []Thread,

    pub fn init(alloc: std.mem.Allocator, p: *const Program) error{OutOfMemory}!Scratch {
        const n = p.states;
        const a_threads = try alloc.alloc(Thread, n);
        errdefer alloc.free(a_threads);
        const a_stamp = try alloc.alloc(u64, n);
        errdefer alloc.free(a_stamp);
        const b_threads = try alloc.alloc(Thread, n);
        errdefer alloc.free(b_threads);
        const b_stamp = try alloc.alloc(u64, n);
        errdefer alloc.free(b_stamp);
        const stack = try alloc.alloc(Thread, n + 1);
        @memset(a_stamp, 0);
        @memset(b_stamp, 0);
        return .{ .a = .{ .threads = a_threads, .stamp = a_stamp }, .b = .{ .threads = b_threads, .stamp = b_stamp }, .stack = stack };
    }

    pub fn deinit(s: *Scratch, alloc: std.mem.Allocator) void {
        alloc.free(s.a.threads);
        alloc.free(s.a.stamp);
        alloc.free(s.b.threads);
        alloc.free(s.b.stamp);
        alloc.free(s.stack);
    }
};

pub const Found = struct {
    /// The match is `s[start..end]`.
    start: usize,
    end: usize,
    /// Each group's `[start, end]`, if the match passed through it, group 1
    /// first; those past the pattern's `groups` are null.
    groups: [groups_most]?[2]usize,
};

pub const Searched = struct {
    found: ?Found,
    /// Positions examined, from 0 to the string's length. A search costs
    /// this times the program's instructions.
    positions: u64,
    /// Instructions followed; tests assert it stays at most twice the
    /// program's instructions per position.
    work: u64,
    /// False when `positions_most` stopped the search early.
    finished: bool,
};

/// Follows every path from `first` to a consuming instruction or `match`,
/// adding the threads to `list` in order of preference at position `pos`. A
/// path reaching an instruction already visited here ends there (an empty loop
/// iteration, or a less preferred path), so each instruction is followed at
/// most once per position.
fn add(p: *const Program, sc: *Scratch, list: *List, first: Thread, pos: usize, s: []const u8, work: *u64) void {
    const len = s.len;
    var top: usize = 1;
    sc.stack[0] = first;
    while (top > 0) {
        top -= 1;
        var t = sc.stack[top];
        while (true) {
            work.* += 1;
            const st = stateOf(p, t.pc, t.flags);
            if (list.stamp[st] == list.now) break;
            list.stamp[st] = list.now;
            const i = p.insts[t.pc];
            switch (i.op) {
                .turn => {
                    t.flags &= ~(@as(u16, 1) << @intCast(i.x));
                    t.pc += 1;
                },
                .again => if (t.flags & (@as(u16, 1) << @intCast(i.x)) != 0) {
                    t.pc += 1;
                } else break,
                .first => t.pc = if (t.flags & (@as(u16, 1) << @intCast(i.x)) != 0) t.pc + 1 else i.y,
                .jmp => t.pc = i.x,
                .split => {
                    sc.stack[top] = .{ .pc = i.y, .caps = t.caps, .flags = t.flags };
                    top += 1;
                    t.pc = i.x;
                },
                .save => {
                    t.caps[1 + i.x] = pos;
                    t.pc += 1;
                },
                .start => if (pos == 0) {
                    t.pc += 1;
                } else break,
                .end => if (pos == len) {
                    t.pc += 1;
                } else break,
                .boundary => if (((pos > 0 and wordByte(s[pos - 1])) != (pos < len and wordByte(s[pos]))) == (i.c == 0)) {
                    t.pc += 1;
                } else break,
                .byte, .any, .set, .match => {
                    list.threads[list.len] = t;
                    list.len += 1;
                    break;
                },
            }
        }
    }
}

/// Returns the leftmost match of `p` in `s`, the one a backtracking matcher
/// would find first, examining at most `positions_most` positions.
pub fn search(p: *const Program, sc: *Scratch, s: []const u8, positions_most: u64) Searched {
    return searchFrom(p, sc, s, 0, positions_most);
}

/// `search` starting at byte `from` of `s`: the leftmost match that starts
/// there or later, with `^`, `$` and `\b` still seeing the whole of `s`.
pub fn searchFrom(p: *const Program, sc: *Scratch, s: []const u8, from: usize, positions_most: u64) Searched {
    std.debug.assert(from <= s.len);
    var clist = &sc.a;
    var nlist = &sc.b;
    clist.reset();
    var matched: ?Caps = null;
    var match_end: usize = 0;
    var positions: u64 = 0;
    var work: u64 = 0;
    var pos: usize = from;
    while (true) : (pos += 1) {
        if (positions == positions_most) return .{ .found = null, .positions = positions, .work = work, .finished = false };
        positions += 1;
        // A new start at each position, least preferred, until a match.
        if (matched == null and (!p.anchored or pos == 0))
            add(p, sc, clist, .{ .pc = 0, .caps = startCaps(pos) }, pos, s, &work);
        nlist.reset();
        for (clist.threads[0..clist.len]) |t| {
            work += 1;
            const i = p.insts[t.pc];
            if (i.op == .match) {
                // Cut every less preferred thread.
                matched = t.caps;
                match_end = pos;
                break;
            }
            if (pos == s.len) continue;
            // `add` leaves only consuming instructions and `match` on a list.
            const ok = switch (i.op) {
                .byte => s[pos] == i.c,
                .any => s[pos] != '\n',
                else => set: {
                    std.debug.assert(i.op == .set);
                    break :set p.sets[i.x].isSet(s[pos]);
                },
            };
            if (ok) add(p, sc, nlist, .{ .pc = t.pc + 1, .caps = t.caps, .flags = std.math.maxInt(u16) }, pos + 1, s, &work);
        }
        std.mem.swap(*List, &clist, &nlist);
        if (pos == s.len) break;
        if (clist.len == 0 and (matched != null or p.anchored)) break;
    }
    const caps = matched orelse return .{ .found = null, .positions = positions, .work = work, .finished = true };
    return .{
        .found = .{ .start = caps[0], .end = match_end, .groups = groupsOf(caps) },
        .positions = positions,
        .work = work,
        .finished = true,
    };
}

/// The number of compiled patterns a run caches, keyed by text and `nocase`,
/// each with its scratch. A new pattern evicts the least recently used.
pub const cache_most = 8;

pub const Cached = struct {
    text: []u8,
    nocase: bool,
    program: Program,
    scratch: Scratch,
    used: u64,

    fn deinit(c: *Cached, alloc: std.mem.Allocator) void {
        alloc.free(c.text);
        c.program.deinit(alloc);
        c.scratch.deinit(alloc);
    }
};

pub const Cache = struct {
    entries: [cache_most]?Cached = @splat(null),
    clock: u64 = 0,

    /// Returns the cached entry for the pattern, if any, marking it used.
    pub fn get(c: *Cache, pattern: []const u8, options: Options) ?*Cached {
        c.clock += 1;
        for (&c.entries) |*e| if (e.*) |*got| if (got.nocase == options.nocase and std.mem.eql(u8, got.text, pattern)) {
            got.used = c.clock;
            return got;
        };
        return null;
    }

    /// Compiles and caches a pattern `get` did not find.
    pub fn put(c: *Cache, alloc: std.mem.Allocator, pattern: []const u8, options: Options, refused: *Refusal) Error!*Cached {
        var program = try compile(alloc, pattern, options, refused);
        errdefer program.deinit(alloc);
        var scratch = try Scratch.init(alloc, &program);
        errdefer scratch.deinit(alloc);
        const text = try alloc.dupe(u8, pattern);
        var slot: usize = 0;
        for (c.entries, 0..) |e, i| {
            const e_used = if (e) |got| got.used else 0;
            const slot_used = if (c.entries[slot]) |got| got.used else 0;
            if (e == null or e_used < slot_used) slot = i;
            if (e == null) break;
        }
        if (c.entries[slot]) |*old| old.deinit(alloc);
        c.entries[slot] = .{ .text = text, .nocase = options.nocase, .program = program, .scratch = scratch, .used = c.clock };
        return &c.entries[slot].?;
    }

    pub fn deinit(c: *Cache, alloc: std.mem.Allocator) void {
        for (&c.entries) |*e| if (e.*) |*got| got.deinit(alloc);
        c.* = .{};
    }
};
