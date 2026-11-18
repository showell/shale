//! `shale_tokens`: a Shale file's tokens, read by the lexer the language itself
//! runs on (`script.lex`), so a tool that reads Shale holds no lexing rules of
//! its own.

const std = @import("std");
const shale = @import("shale");
const script = shale.script;
const lex = script.lex;
const ast = script.ast;
const Run = script.interp.Run;
const Value = script.value.Value;
const Error = script.interp.Error;
const zig_host = @import("shale_host_zig.zig");

/// `host("shale_tokens", path[, {tag}])`: the file's tokens in order, comments
/// among them, as `{tag, text, line, start, stop}`. `tag` is a `lex.Tag` name.
/// `text` is the token's source bytes, a string's quotes and escapes as written,
/// and `start` and `stop` are `zig_decls`' span: `read(path, start, stop - start)`
/// is the token. Only space stands between tokens. A file that doesn't lex stops
/// the run, naming the lexer's line and message. Charges a step per token.
pub fn tokens(backing: std.mem.Allocator, run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    if (args.len < 1 or args.len > 2) return run.fail(pos, .argument, "`shale_tokens` takes a path and options, given {d} arguments", .{args.len});
    const only: ?lex.Tag = if (args.len == 2) try zig_host.tagOption(lex.Tag, run, pos, "shale_tokens", "`name`, `int`, `string`, `comment`, `kw_end`, `plus`, ...", args[1]) else null;
    if (only == .eof) return run.fail(pos, .argument, "`shale_tokens`: no token tag `eof`: the end of a file is no token here", .{});
    const hub = script.builtins_store;
    const rd = script.builtins_read;
    const p = try hub.pathArg(run, pos, .host, args[0], false);
    try hub.countRead(run, pos);
    const size = try rd.fileSize(run, pos, .host, p);
    var scratch: zig_host.Scratch = undefined;
    const sa = scratch.init(backing, run);
    defer scratch.deinit();
    const src = try rd.readSizedIn(sa, run, pos, .host, p, size, 0, null);
    try script.builtins.chargeBytes(run, pos, src.len);
    var lexer = lex.Lexer.init(sa, src);
    const list = try run.newTable(pos);
    list.kind = .list;
    while (true) {
        const token = lexer.nextOrComment() catch |e| switch (e) {
            error.OutOfMemory => return run.oom(pos),
            error.Lex => {
                const refusal = lexer.err.?;
                return run.fail(pos, .argument, "`shale_tokens`: {s} does not lex as Shale at line {d}: {s}", .{ p.rel(), refusal.line, refusal.message });
            },
        };
        if (token.tag == .eof) return .{ .table = list };
        try run.charge(pos, 1);
        if (only) |o| if (token.tag != o) continue;
        const m = try run.newTable(pos);
        m.kind = .map;
        try zig_host.put(run, pos, m, "tag", .{ .string = @tagName(token.tag) });
        try zig_host.put(run, pos, m, "text", .{ .string = try zig_host.keep(run, pos, src[token.start..token.stop]) });
        try zig_host.put(run, pos, m, "line", .{ .int = @intCast(token.line) });
        try zig_host.put(run, pos, m, "start", .{ .int = @intCast(token.start) });
        try zig_host.put(run, pos, m, "stop", .{ .int = @intCast(token.stop) });
        list.list.append(run.alloc, .{ .table = m }) catch return run.oom(pos);
    }
}
