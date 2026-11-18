//! A generator of well-formed scripts, for the hunts (src/script/gen_hunt_test.zig):
//! every statement and expression form of docs/SHALE.md's grammar, from a
//! `std.Random`, so a blind seed and an explorer's tape drive it alike.
//!
//! What it makes parses, by construction: names are declared before they are
//! read, a function sees only its parameters, its locals and the consts above
//! it, and is called only below its own line; `break` and `continue` stand
//! only in a loop, and `return` only last in its block. Each local has a type
//! (an integer, a string, a boolean, a list of integers, or a map from
//! strings to integers, keys `a`, `b` and `c` always among them), and an
//! expression is made for the type it is given, so most scripts run to their
//! end. Loops are bounded and recursion counts down a depth.
//!
//! A run that finishes says little about the stops, so each script has a
//! **mischief** rate, from none to a few in a hundred choices: an operand of
//! the wrong type, an index past a list's end, a key no map has, a divisor
//! that can be zero, an integer at its extreme, a string doubled in a loop.
//! Each reaches a stop of its own class at run time, never a parse's.

const std = @import("std");

/// What a script may hold in a name.
pub const Ty = enum { int, str, bool, list, map };

const Local = struct {
    name: []const u8,
    ty: Ty,
    /// A loop's variable and a function's depth are read, never assigned.
    fixed: bool,
};

const Function = struct {
    name: []const u8,
    params: []const Ty,
    returns: Ty,
};

pub const Options = struct {
    /// Choices, in a thousand, that make mischief; null takes one at random
    /// for the script, from 0 to `mischief_most`.
    mischief: ?u16 = null,
    mischief_most: u16 = 40,
    /// Whether the script may change the store (then it is not a background
    /// run's).
    store: bool = true,
    /// Statements at the top level, at most.
    statements_most: u32 = 24,
};

pub const Gen = struct {
    r: std.Random,
    a: std.mem.Allocator,
    out: std.ArrayList(u8) = .empty,
    o: Options,
    mischief: u16 = 0,
    /// The names in scope, innermost last; `marks` holds where each block's
    /// began.
    locals: std.ArrayList(Local) = .empty,
    marks: std.ArrayList(usize) = .empty,
    consts: std.ArrayList(Local) = .empty,
    functions: std.ArrayList(Function) = .empty,
    /// The function being declared, which may call itself; null at the top.
    current: ?Function = null,
    loops: u32 = 0,
    nest: u32 = 0,
    names: u32 = 0,
    /// Whether "gen/a.txt" is written before any statement reads it.
    has_file: bool = false,

    const Error = error{OutOfMemory};
    const nest_most = 4;
    const expr_most = 4;

    pub fn init(r: std.Random, a: std.mem.Allocator, o: Options) Gen {
        var g: Gen = .{ .r = r, .a = a, .o = o };
        g.mischief = o.mischief orelse r.uintAtMost(u16, o.mischief_most);
        return g;
    }

    /// The script. Its bytes are `a`'s, as everything the generator holds.
    pub fn script(g: *Gen) Error![]const u8 {
        if (g.o.store and g.chance(500)) {
            try g.put("replace(\"gen/a.txt\", \"one\\ntwo\\n\")\n");
            g.has_file = true;
        }
        for (0..g.r.uintAtMost(usize, 2)) |_| try g.constant();
        for (0..g.r.uintAtMost(usize, 3)) |_| try g.function();
        try g.open();
        const n = g.r.intRangeAtMost(u32, 1, g.o.statements_most);
        for (0..n) |_| try g.statement();
        if (g.chance(100)) {
            try g.put("return\n");
        }
        g.close();
        return g.out.items;
    }

    fn chance(g: *Gen, per_mille: u16) bool {
        return g.r.uintLessThan(u16, 1000) < per_mille;
    }

    fn naughty(g: *Gen) bool {
        return g.mischief > 0 and g.chance(g.mischief);
    }

    fn put(g: *Gen, s: []const u8) Error!void {
        try g.out.appendSlice(g.a, s);
    }

    fn print(g: *Gen, comptime fmt: []const u8, values: anytype) Error!void {
        try g.out.print(g.a, fmt, values);
    }

    fn fresh(g: *Gen, prefix: []const u8) Error![]const u8 {
        g.names += 1;
        return std.fmt.allocPrint(g.a, "{s}{d}", .{ prefix, g.names });
    }

    fn open(g: *Gen) Error!void {
        try g.marks.append(g.a, g.locals.items.len);
    }

    fn close(g: *Gen) void {
        g.locals.shrinkRetainingCapacity(g.marks.pop().?);
    }

    fn declare(g: *Gen, name: []const u8, ty: Ty, fixed: bool) Error!void {
        try g.locals.append(g.a, .{ .name = name, .ty = ty, .fixed = fixed });
    }

    /// A name of type `ty` in scope (a local, a parameter or a const the
    /// place may see), or null; `assignable` leaves out the fixed and the
    /// consts.
    fn pickName(g: *Gen, ty: Ty, assignable: bool) ?[]const u8 {
        var n: usize = 0;
        for (g.locals.items) |l| {
            if (l.ty == ty and !(assignable and l.fixed)) n += 1;
        }
        if (!assignable) for (g.consts.items) |c| {
            if (c.ty == ty) n += 1;
        };
        if (n == 0) return null;
        var k = g.r.uintLessThan(usize, n);
        for (g.locals.items) |l| {
            if (l.ty == ty and !(assignable and l.fixed)) {
                if (k == 0) return l.name;
                k -= 1;
            }
        }
        for (g.consts.items) |c| {
            if (c.ty == ty) {
                if (k == 0) return c.name;
                k -= 1;
            }
        }
        unreachable;
    }

    fn anyType(g: *Gen) Ty {
        return g.r.enumValue(Ty);
    }

    fn constant(g: *Gen) Error!void {
        const ty: Ty = if (g.r.boolean()) .int else .str;
        const name = try g.fresh("C");
        try g.print("const {s} = ", .{name});
        try g.expr(ty, 2);
        try g.put("\n");
        // Seen below its line, by the top level and the functions after it.
        try g.consts.append(g.a, .{ .name = name, .ty = ty, .fixed = true });
    }

    fn function(g: *Gen) Error!void {
        const name = try g.fresh("f");
        const n = g.r.uintAtMost(usize, 3);
        const params = try g.a.alloc(Ty, n);
        for (params) |*p| p.* = g.anyType();
        const f: Function = .{ .name = name, .params = params, .returns = g.anyType() };
        try g.print("function {s}(d", .{name});
        const saved_locals = g.locals;
        const saved_marks = g.marks;
        g.locals = .empty;
        g.marks = .empty;
        try g.open();
        try g.declare("d", .int, true);
        for (params) |ty| {
            const p = try g.fresh("p");
            try g.print(", {s}", .{p});
            try g.declare(p, ty, false);
        }
        try g.put(")\n");
        // The base case before the function may call itself, so its depth ends it.
        try g.put("if d <= 0 then return ");
        try g.expr(f.returns, 2);
        try g.put(" end\n");
        g.current = f;
        for (0..g.r.uintAtMost(usize, 4)) |_| try g.statement();
        try g.put("return ");
        try g.expr(f.returns, expr_most);
        try g.put("\nend\n");
        g.current = null;
        g.close();
        g.locals = saved_locals;
        g.marks = saved_marks;
        try g.functions.append(g.a, f);
    }

    fn block(g: *Gen, most: usize) Error!void {
        g.nest += 1;
        defer g.nest -= 1;
        try g.open();
        defer g.close();
        const n = g.r.uintAtMost(usize, most);
        for (0..n) |_| try g.statement();
        // A loop's turn may end early.
        if (g.loops > 0 and g.chance(80)) try g.put(if (g.r.boolean()) "break\n" else "continue\n");
    }

    fn statement(g: *Gen) Error!void {
        const deep = g.nest >= nest_most;
        switch (g.r.uintLessThan(u8, 20)) {
            0, 1, 2, 3 => {
                const ty = g.anyType();
                const name = try g.fresh("v");
                try g.print("local {s} = ", .{name});
                try g.expr(ty, expr_most);
                try g.put("\n");
                try g.declare(name, ty, false);
            },
            4, 5 => {
                const ty = g.anyType();
                const name = g.pickName(ty, true) orelse return g.statement();
                try g.print("{s} = ", .{name});
                try g.expr(ty, expr_most);
                try g.put("\n");
            },
            6 => {
                const l = g.pickName(.list, false) orelse return g.statement();
                if (g.r.uintLessThan(u8, 4) == 0) {
                    try g.print("push({s}, ", .{l});
                    try g.expr(.int, 3);
                    try g.put(")\n");
                    return;
                }
                if (g.naughty()) {
                    try g.print("{s}[#{s} + 3] = ", .{ l, l });
                } else switch (g.r.uintLessThan(u8, 3)) {
                    0 => try g.print("{s}[#{s}] = ", .{ l, l }),
                    1 => try g.print("{s}[#{s} + 1] = ", .{ l, l }),
                    else => try g.print("{s}[1] = ", .{l}),
                }
                try g.expr(.int, 3);
                try g.put("\n");
            },
            7 => {
                const m = g.pickName(.map, false) orelse return g.statement();
                if (g.r.boolean()) {
                    try g.print("{s}.{s} = ", .{ m, g.key() });
                } else {
                    try g.print("{s}[\"k\" .. ", .{m});
                    try g.expr(.int, 2);
                    try g.put("] = ");
                }
                try g.expr(.int, 3);
                try g.put("\n");
            },
            8, 9 => {
                try g.put("print(");
                const n = g.r.intRangeAtMost(usize, 1, 3);
                for (0..n) |i| {
                    if (i > 0) try g.put(", ");
                    // Only integers, strings and booleans print.
                    try g.expr(switch (g.r.uintLessThan(u8, 3)) {
                        0 => .int,
                        1 => .str,
                        else => .bool,
                    }, 3);
                }
                try g.put(")\n");
            },
            10 => if (deep) return g.simple() else {
                try g.put("if ");
                try g.expr(.bool, 3);
                try g.put(" then\n");
                try g.block(4);
                var k = g.r.uintAtMost(usize, 2);
                while (k > 0) : (k -= 1) {
                    try g.put("elseif ");
                    try g.expr(.bool, 3);
                    try g.put(" then\n");
                    try g.block(3);
                }
                if (g.r.boolean()) {
                    try g.put("else\n");
                    try g.block(3);
                }
                try g.put("end\n");
            },
            11 => if (deep) return g.simple() else {
                // Counted first, so a `continue` cannot skip the count.
                const w = try g.fresh("w");
                try g.print("local {s} = 0 while {s} < {d} ", .{ w, w, g.r.intRangeAtMost(u8, 1, 5) });
                if (g.r.boolean()) {
                    try g.put("and ");
                    try g.expr(.bool, 2);
                }
                try g.print(" do {s} = {s} + 1\n", .{ w, w });
                try g.declare(w, .int, true);
                g.loops += 1;
                try g.block(4);
                g.loops -= 1;
                try g.put("end\n");
            },
            12 => if (deep) return g.simple() else {
                const i = try g.fresh("i");
                const from = g.r.intRangeAtMost(i8, -3, 3);
                const span = g.r.intRangeAtMost(i8, 0, 5);
                if (g.r.boolean()) {
                    try g.print("for {s} = {d}, {d} do\n", .{ i, from, from + span });
                } else {
                    const step: i8 = if (g.naughty()) 0 else if (g.r.boolean()) g.r.intRangeAtMost(i8, 1, 3) else -g.r.intRangeAtMost(i8, 1, 3);
                    try g.print("for {s} = {d}, {d}, {d} do\n", .{ i, from, if (step < 0) from - span else from + span, step });
                }
                g.loops += 1;
                try g.open();
                try g.declare(i, .int, true);
                try g.block(4);
                g.close();
                g.loops -= 1;
                try g.put("end\n");
            },
            13 => if (deep) return g.simple() else {
                const x = try g.fresh("x");
                if (g.r.boolean()) {
                    try g.print("for {s} in ", .{x});
                    try g.expr(.list, 2);
                    try g.put(" do\n");
                    g.loops += 1;
                    try g.open();
                    try g.declare(x, .int, true);
                } else {
                    try g.print("for {s} in keys(", .{x});
                    try g.expr(.map, 2);
                    try g.put(") do\n");
                    g.loops += 1;
                    try g.open();
                    try g.declare(x, .str, true);
                }
                try g.block(4);
                g.close();
                g.loops -= 1;
                try g.put("end\n");
            },
            14 => if (deep) return g.simple() else {
                try g.put("do\n");
                try g.block(3);
                try g.put("end\n");
            },
            15 => try g.put("yield()\n"),
            16 => {
                const f = g.callable() orelse return g.statement();
                try g.put("print(attempt(");
                try g.put(f.name);
                try g.args(f, true);
                try g.put(").ok)\n");
            },
            17 => if (g.o.store) {
                try g.put("replace(\"gen/b\" .. (");
                try g.expr(.int, 2);
                try g.put(") % 3, ");
                try g.expr(.str, 3);
                try g.put(")\n");
            } else return g.simple(),
            18 => {
                const l = g.pickName(.list, false) orelse return g.statement();
                try g.print("sort({s})\n", .{l});
            },
            else => return g.simple(),
        }
    }

    /// A statement with no block of its own.
    fn simple(g: *Gen) Error!void {
        const name = try g.fresh("v");
        const ty = g.anyType();
        try g.print("local {s} = ", .{name});
        try g.expr(ty, 2);
        try g.put("\n");
        try g.declare(name, ty, false);
    }

    fn key(g: *Gen) []const u8 {
        if (g.naughty()) return "zz";
        return switch (g.r.uintLessThan(u8, 3)) {
            0 => "a",
            1 => "b",
            else => "c",
        };
    }

    /// A function this place may call: one declared above, or the one being
    /// declared (whose depth then counts down).
    fn callable(g: *Gen) ?Function {
        const n = g.functions.items.len + @intFromBool(g.current != null);
        if (n == 0) return null;
        const k = g.r.uintLessThan(usize, n);
        return if (k < g.functions.items.len) g.functions.items[k] else g.current.?;
    }

    fn args(g: *Gen, f: Function, after_name: bool) Error!void {
        try g.put(if (after_name) ", " else "(");
        const own = g.current != null and std.mem.eql(u8, g.current.?.name, f.name);
        if (own) try g.put("d - 1") else try g.print("{d}", .{g.r.uintAtMost(u8, 3)});
        for (f.params) |ty| {
            try g.put(", ");
            try g.expr(ty, 2);
        }
        if (!after_name) try g.put(")");
    }

    /// An expression of type `ty`, at most `depth` deep.
    fn expr(g: *Gen, ty: Ty, depth: u32) Error!void {
        // Mischief: an operand of another type, where the operator takes
        // only this one.
        // Rarer than the other mischief, which a wrong type would otherwise forestall.
        const want = if (g.naughty() and g.r.uintLessThan(u8, 4) == 0) g.anyType() else ty;
        if (depth == 0) return g.leaf(want);
        if (g.chance(250)) return g.leaf(want);
        if (g.chance(60)) if (g.callable()) |f| if (f.returns == want) {
            try g.put(f.name);
            return g.args(f, false);
        };
        switch (want) {
            .int => switch (g.r.uintLessThan(u8, 14)) {
                0, 1 => try g.binary(" + ", .int, .int, depth),
                2 => try g.binary(" - ", .int, .int, depth),
                3 => try g.binary(" * ", .int, .int, depth),
                4, 5 => {
                    try g.put("(");
                    try g.expr(.int, depth - 1);
                    try g.put(if (g.r.boolean()) " // " else " % ");
                    if (g.naughty()) {
                        try g.put("(");
                        try g.expr(.int, depth - 1);
                        try g.put(" % 2)");
                    } else {
                        try g.put("((");
                        try g.expr(.int, depth - 1);
                        try g.put(") % 7 + 1)");
                    }
                    try g.put(")");
                },
                6 => {
                    try g.put("-(");
                    try g.expr(.int, depth - 1);
                    try g.put(")");
                },
                7 => {
                    try g.put("#");
                    try g.paren(if (g.r.boolean()) .str else .list, depth);
                },
                8 => {
                    const l = g.pickName(.list, false) orelse return g.leaf(.int);
                    if (g.naughty()) try g.print("{s}[#{s} + 1]", .{ l, l }) else try g.print("{s}[#{s}]", .{ l, l });
                },
                9 => {
                    const m = g.pickName(.map, false) orelse return g.leaf(.int);
                    if (g.r.boolean()) {
                        try g.print("{s}.{s}", .{ m, g.key() });
                    } else {
                        try g.print("get({s}, ", .{m});
                        try g.expr(.str, depth - 1);
                        try g.put(", 0)");
                    }
                },
                10 => {
                    const which = g.r.uintLessThan(u8, 3);
                    try g.put(switch (which) {
                        0 => "min(",
                        1 => "max(",
                        else => "abs(",
                    });
                    try g.expr(.int, depth - 1);
                    if (which < 2) {
                        try g.put(", ");
                        try g.expr(.int, depth - 1);
                    }
                    try g.put(")");
                },
                11 => {
                    try g.put("tonumber(tostring(");
                    try g.expr(.int, depth - 1);
                    try g.put("))");
                },
                12 => {
                    try g.put("count(");
                    try g.expr(.map, depth - 1);
                    try g.put(")");
                },
                else => {
                    try g.put("(");
                    try g.expr(.int, depth - 1);
                    try g.put(")");
                },
            },
            .str => switch (g.r.uintLessThan(u8, 8)) {
                0, 1, 2 => try g.binary(" .. ", .str, if (g.r.boolean()) .str else .int, depth),
                3 => {
                    try g.put("sub(");
                    try g.expr(.str, depth - 1);
                    try g.print(", {d}, {d})", .{ g.r.intRangeAtMost(i8, -4, 4), g.r.intRangeAtMost(i8, -4, 6) });
                },
                4 => {
                    try g.put(if (g.r.boolean()) "upper(" else "trim(");
                    try g.expr(.str, depth - 1);
                    try g.put(")");
                },
                5 => {
                    try g.put("tostring(");
                    try g.expr(.int, depth - 1);
                    try g.put(")");
                },
                6 => {
                    try g.put("rep(");
                    try g.expr(.str, depth - 1);
                    try g.print(", {d})", .{if (g.naughty()) @as(u32, 1_000_000) else g.r.uintAtMost(u32, 4)});
                },
                else => if (g.has_file) {
                    try g.put("read(\"gen/a.txt\")");
                } else try g.leaf(.str),
            },
            .bool => switch (g.r.uintLessThan(u8, 9)) {
                0, 1 => try g.binary(switch (g.r.uintLessThan(u8, 6)) {
                    0 => " < ",
                    1 => " <= ",
                    2 => " > ",
                    3 => " >= ",
                    4 => " == ",
                    else => " ~= ",
                }, .int, .int, depth),
                2 => try g.binary(if (g.r.boolean()) " == " else " < ", .str, .str, depth),
                3 => try g.binary(if (g.r.boolean()) " and " else " or ", .bool, .bool, depth),
                4 => {
                    try g.put("not ");
                    try g.paren(.bool, depth);
                },
                5 => {
                    const m = g.pickName(.map, false) orelse return g.leaf(.bool);
                    try g.print("has_key({s}, ", .{m});
                    try g.expr(.str, depth - 1);
                    try g.put(")");
                },
                6 => {
                    try g.put("contains(");
                    try g.expr(.str, depth - 1);
                    try g.put(", ");
                    try g.expr(.str, depth - 1);
                    try g.put(")");
                },
                7 => {
                    const t = g.anyType();
                    const name = g.pickName(t, false) orelse return g.leaf(.bool);
                    try g.print("{s} == nil", .{name});
                },
                else => if (g.o.store) {
                    try g.put("exists(\"gen/a.txt\")");
                } else try g.leaf(.bool),
            },
            .list => {
                const n = g.r.intRangeAtMost(usize, 1, 4);
                try g.put("{");
                for (0..n) |i| {
                    if (i > 0) try g.put(", ");
                    try g.expr(.int, depth - 1);
                }
                try g.put("}");
            },
            .map => {
                try g.put("{a = ");
                try g.expr(.int, depth - 1);
                try g.put(", b = ");
                try g.expr(.int, depth - 1);
                try g.put(", c = ");
                try g.expr(.int, depth - 1);
                try g.put("}");
            },
        }
    }

    fn paren(g: *Gen, ty: Ty, depth: u32) Error!void {
        try g.put("(");
        try g.expr(ty, depth - 1);
        try g.put(")");
    }

    fn binary(g: *Gen, op: []const u8, left: Ty, right: Ty, depth: u32) Error!void {
        try g.put("(");
        try g.expr(left, depth - 1);
        try g.put(op);
        try g.expr(right, depth - 1);
        try g.put(")");
    }

    /// A name or a literal of type `ty`.
    fn leaf(g: *Gen, ty: Ty) Error!void {
        if (g.r.boolean()) if (g.pickName(ty, false)) |name| return g.put(name);
        switch (ty) {
            .int => if (g.naughty()) {
                try g.put(switch (g.r.uintLessThan(u8, 3)) {
                    0 => "9223372036854775807",
                    1 => "(-9223372036854775807 - 1)",
                    else => "4611686018427387904",
                });
            } else try g.print("{d}", .{g.r.intRangeAtMost(i16, -20, 100)}),
            .str => try g.put(switch (g.r.uintLessThan(u8, 8)) {
                0 => "\"\"",
                1 => "\"a\"",
                2 => "\"hello\"",
                3 => "\"x,y\"",
                4 => "\"tab\\there\"",
                5 => "'q\\x41\\n'",
                6 => "[=[raw ]] \"text\"]=]",
                else => "\"12\"",
            }),
            .bool => try g.put(if (g.r.boolean()) "true" else "false"),
            .list => try g.print("{{{d}}}", .{g.r.uintAtMost(u8, 9)}),
            .map => try g.put("{a = 1, b = 2, c = 3}"),
        }
    }
};
