//! `re_lines`: the lines of a string a regular expression matches, searched
//! here by the regex builtins' own engine, pattern cache and charges
//! (`script.builtins_regex`), so a pattern over megabytes costs what its
//! searches do and not a script's loop over every line.

const std = @import("std");
const shale = @import("shale");
const script = shale.script;
const ast = script.ast;
const bi = script.builtins;
const br = script.builtins_regex;
const regex = script.regex;
const Run = script.interp.Run;
const Value = script.value.Value;
const Error = script.interp.Error;

/// `host("re_lines", s, pattern[, {invert, nocase}])`: the lines of `s`, split
/// as `lines` splits them, that `pattern` matches (`invert`: the others), each
/// `{line, text}`, numbered from 1; none is `{}`. Each line is searched alone,
/// so `^`, `$` and `\b` see its edges. Charged a step for each 16 bytes of `s`
/// (the split), a step a line, a step a line kept, and each line's search.
pub fn lines(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    if (args.len < 2 or args.len > 3) return run.fail(pos, .argument, "`re_lines` takes a text, a pattern and options, given {d} arguments", .{args.len});
    const s = try text(run, pos, args[0], "its first argument");
    const pattern = try text(run, pos, args[1], "its pattern");
    var options: regex.Options = .{};
    var invert = false;
    if (args.len == 3) {
        const m = switch (args[2]) {
            .table => |t| if (t.kind == .list) return run.fail(pos, .type, "`re_lines` takes its options as a map, given a list", .{}) else t,
            else => |v| return run.fail(pos, .type, "`re_lines` takes its options as a map, not {s}", .{v.typeName()}),
        };
        // Every name first, then the values, as the prelude's did.
        for (m.map.keys()) |k| {
            if (!std.mem.eql(u8, k, "nocase") and !std.mem.eql(u8, k, "invert"))
                return run.fail(pos, .fail, "`re_lines` takes `invert` and `nocase`, not `{s}`", .{k});
        }
        if (m.map.get("nocase")) |x| options.nocase = try br.flagOption(run, pos, "re_lines", "nocase", x);
        if (m.map.get("invert")) |x| invert = try br.flagOption(run, pos, "re_lines", "invert", x);
    }
    const c = try br.program(run, pos, "re_lines", pattern, options);
    try bi.chargeBytes(run, pos, s.len);
    run.tally(s.len);
    const out = try run.newTable(pos);
    var each: br.Searches = .{};
    var n: i64 = 0;
    var rest = s;
    while (rest.len > 0) {
        const nl = std.mem.indexOfScalar(u8, rest, '\n');
        const line = if (nl) |i| rest[0..i] else rest;
        rest = if (nl) |i| rest[i + 1 ..] else "";
        n += 1;
        try run.charge(pos, 1);
        run.tally(1);
        if ((try each.from(run, pos, c, line, 0) != null) == invert) continue;
        try run.charge(pos, 1);
        const m = try run.newTable(pos);
        m.kind = .map;
        m.map.put(run.alloc, "line", .{ .int = n }) catch return run.oom(pos);
        m.map.put(run.alloc, "text", .{ .string = line }) catch return run.oom(pos);
        // A list once it holds a line, `{}` as the prelude's was while it holds none.
        out.kind = .list;
        out.list.append(run.alloc, .{ .table = m }) catch return run.oom(pos);
    }
    try each.done(run, pos);
    return .{ .table = out };
}

fn text(run: *Run, pos: ast.Pos, v: Value, what: []const u8) Error![]const u8 {
    return switch (v) {
        .string => |x| x,
        else => run.fail(pos, .type, "`re_lines`: {s} must be a string, not {s}", .{ what, v.typeName() }),
    };
}
