//! The store's `walk` builtin: every entry under a folder, a part at a time,
//! each part resumed where the last one ended.

const std = @import("std");
const ast = @import("ast.zig");
const interp = @import("interp.zig");
const value = @import("value.zig");
const bi = @import("builtins.zig");
const text = @import("builtins_text.zig");

const store = @import("webbox").store;

const Run = interp.Run;
const Value = value.Value;
const Error = interp.Error;
const B = ast.Builtin;
const spec = bi.spec;
const hub = @import("builtins_store.zig");
const modifiedSeconds = hub.modifiedSeconds;
const Path = hub.Path;
const pathArg = hub.pathArg;
const storeFailed = hub.storeFailed;
const countRead = hub.countRead;

pub const specs = [_]bi.Spec{
    spec(.walk, 1, 2, walk),
};

/// Lists a folder's entries sorted by name, charged as `ls` is: a step per
/// entry plus the sort's bound. The caller counts the read.
pub fn sortedEntries(run: *Run, pos: ast.Pos, b: B, full: []const u8, rel: []const u8) Error![]store.Entry {
    const entries = store.list(run.io, run.alloc, full) catch |e| return storeFailed(run, pos, b, rel, e);
    return sortEntries(run, pos, entries);
}

/// Like `sortedEntries`, but returns null for a folder that vanished during a
/// background run, which reads while others write.
fn sortedEntriesOrGone(run: *Run, pos: ast.Pos, b: B, full: []const u8, rel: []const u8) Error!?[]store.Entry {
    const entries = store.list(run.io, run.alloc, full) catch |e| {
        if (run.read_only and e == error.FileNotFound) return null;
        return storeFailed(run, pos, b, rel, e);
    };
    return try sortEntries(run, pos, entries);
}

fn sortEntries(run: *Run, pos: ast.Pos, entries: []store.Entry) Error![]store.Entry {
    var name_bytes: u64 = 0;
    for (entries) |en| name_bytes += en.name.len;
    try run.charge(pos, entries.len + bi.sortBound(entries.len, name_bytes));
    var ctx: bi.SortContext = .{};
    std.sort.pdq(store.Entry, entries, &ctx, struct {
        fn lt(c: *bi.SortContext, x: store.Entry, y: store.Entry) bool {
            c.comparisons += 1;
            c.bytes += @min(x.name.len, y.name.len);
            return std.mem.lessThan(u8, x.name, y.name);
        }
    }.lt);
    run.tally(entries.len + ctx.comparisons + ctx.bytes);
    return entries;
}

/// Called between a folder's listing and an entry's stat, for tests: an entry
/// removed there simulates one gone under a background run.
pub var between_list_and_stat_for_test: ?*const fn () void = null;

/// A folder the walk is in: its entries, and the index of the next to take.
const Level = struct { rel: []const u8, full: []const u8, entries: []store.Entry, at: usize = 0, depth: u64 };

/// Joins an entry's name to its folder's path. A mount's root (`m:`) takes the
/// name with no `/`.
fn under(run: *Run, pos: ast.Pos, folder: []const u8, name: []const u8) Error![]const u8 {
    if (folder.len == 0) return name;
    const sep: []const u8 = if (folder[folder.len - 1] == ':') "" else "/";
    return std.fmt.allocPrint(run.alloc, "{s}{s}{s}", .{ folder, sep, name }) catch run.oom(pos);
}

/// Restores the walk to where `after` left it: each folder on `after`'s path is
/// listed again, positioned at the first name past `after`'s part, so a walk
/// in parts takes each entry once whatever came or went in between. `after` is
/// a path the walk returned, under `p`; "" or `p` itself means the start.
fn walkResume(run: *Run, pos: ast.Pos, p: Path, o: WalkOptions, after: []const u8, levels: *std.ArrayList(Level)) Error!void {
    if (after.len == 0 or std.mem.eql(u8, after, p.rel)) return;
    const skip = p.rel.len + @intFromBool(p.rel.len > 0 and p.rel[p.rel.len - 1] != ':');
    const rest = if (p.rel.len == 0) after else if (after.len > skip and std.mem.startsWith(u8, after, p.rel) and (skip == p.rel.len or after[p.rel.len] == '/')) after[skip..] else return run.fail(pos, .argument, "`walk`: `after` is \"{s}\", which is not under \"{s}\": it is a path an earlier walk of it returned", .{ after, p.rel });
    var parts = std.mem.splitScalar(u8, rest, '/');
    while (parts.next()) |part| {
        const top = &levels.items[levels.items.len - 1];
        var i: usize = 0;
        while (i < top.entries.len and std.mem.lessThan(u8, top.entries[i].name, part)) i += 1;
        try run.charge(pos, i + 1);
        run.tally(i + 1);
        if (i == top.entries.len or !std.mem.eql(u8, top.entries[i].name, part)) {
            top.at = i;
            return;
        }
        top.at = i + 1;
        const en = top.entries[i];
        if (en.kind != .directory or (o.depth != null and top.depth >= o.depth.?)) return;
        const rel = try under(run, pos, top.rel, en.name);
        const full = std.fmt.allocPrint(run.alloc, "{s}/{s}", .{ top.full, en.name }) catch return run.oom(pos);
        try countRead(run, pos);
        const entries = (try sortedEntriesOrGone(run, pos, .walk, full, rel)) orelse return;
        const depth = top.depth + 1;
        levels.append(run.alloc, .{ .rel = rel, .full = full, .entries = entries, .depth = depth }) catch return run.oom(pos);
    }
}

/// What `walk`'s second argument asks: how deep, and which entries.
const WalkOptions = struct {
    /// How deep to walk; 1 lists only the folder's own entries.
    depth: ?u64 = null,
    /// Keep only entries whose names match (`like`); folders are walked into
    /// whether they match or not.
    like: ?[]const u8 = null,
    /// Keep only entries of this kind.
    kind: ?store.Kind = null,
    /// Where an earlier call stopped: the walk continues after this path, in
    /// its order, whether the path still exists or not.
    after: ?[]const u8 = null,
    /// Names to pass over, files and folders alike, as a `like` pattern; a
    /// skipped folder is never entered. `.*` leaves out `.git`, `.zig-cache`
    /// and every other hidden name.
    skip: ?[]const u8 = null,
    /// Reads to leave in the budget. The part ends, with its `more` entry,
    /// before an entry could take the remaining reads below this, so a script
    /// that walks as far as its budget allows can still report what it found.
    /// Counts reads only, so it is deterministic.
    keep_reads: ?u64 = null,
};

/// The most reads one entry of a walk takes: its stat, and its listing if
/// it is a folder walked into.
const entry_reads = 2;

fn walkOptions(run: *Run, pos: ast.Pos, v: Value) Error!WalkOptions {
    const m = try bi.mapArg(run, pos, .walk, v);
    var o: WalkOptions = .{};
    for (m.map.keys(), m.map.values()) |k, x| {
        if (std.mem.eql(u8, k, "depth")) {
            const d = try bi.intArg(run, pos, .walk, x, "its depth");
            if (d < 1) return run.fail(pos, .argument, "`walk`: a depth of {d}: 1 is the folder's own entries", .{d});
            o.depth = @intCast(d);
        } else if (std.mem.eql(u8, k, "like")) {
            o.like = try bi.stringArg(run, pos, .walk, x, "its `like`");
        } else if (std.mem.eql(u8, k, "after")) {
            o.after = try bi.stringArg(run, pos, .walk, x, "its `after`");
        } else if (std.mem.eql(u8, k, "skip")) {
            o.skip = try bi.stringArg(run, pos, .walk, x, "its `skip`");
        } else if (std.mem.eql(u8, k, "keep_reads")) {
            const n = try bi.intArg(run, pos, .walk, x, "its `keep_reads`");
            if (n < 0) return run.fail(pos, .argument, "`walk`: `keep_reads` of {d}: it is a count of reads", .{n});
            o.keep_reads = @intCast(n);
        } else if (std.mem.eql(u8, k, "kind")) {
            const s = try bi.stringArg(run, pos, .walk, x, "its `kind`");
            o.kind = std.meta.stringToEnum(store.Kind, s) orelse return run.fail(pos, .argument, "`walk`: a kind of \"{s}\": \"file\", \"directory\" or \"other\"", .{s});
        } else return run.fail(pos, .argument, "`walk` takes `depth`, `like`, `kind`, `skip`, `after` and `keep_reads`, not `{s}`", .{k});
    }
    return o;
}

/// `walk(p[, {depth, like, kind, after, skip, keep_reads}])` returns every
/// entry under `p`, depth first, each folder's names sorted and a folder
/// before its contents, as `{path, size, kind, modified}` with `path` relative
/// to the root. Each folder listed costs one read and each entry kept one
/// stat. A folder's size is 0, since the hosts disagree on it. `depth` limits
/// it (1 is `ls` with each entry's stat); `like` and `kind` filter entries, and
/// an entry filtered out is never stat'ed. The walk keeps its own stack, not
/// the Zig stack, since a tree's depth is the store's to choose.
fn walk(run: *Run, pos: ast.Pos, args: []const Value) Error!Value {
    const p = try pathArg(run, pos, .walk, args[0], true);
    const o: WalkOptions = if (args.len > 1) try walkOptions(run, pos, args[1]) else .{};
    const a = run.alloc;
    // If the walk's own stat and listing, the listings to resume at `after`,
    // and one entry would eat into `keep_reads`, we read nothing and return
    // only the `more` entry, unchanged.
    if (o.keep_reads) |keep| {
        const head = 2 + if (o.after) |after| std.mem.count(u8, after, "/") + 1 else 0;
        if (run.budgets.reads -| run.reads < keep +| head +| entry_reads) {
            const out = try run.newTable(pos);
            out.kind = .list;
            out.list.append(a, try moreEntry(run, pos, o.after orelse "")) catch return run.oom(pos);
            return .{ .table = out };
        }
    }
    try countRead(run, pos);
    const st = (store.statOrNull(run.io, a, p.full) catch |e| return storeFailed(run, pos, .walk, p.rel, e)) orelse return storeFailed(run, pos, .walk, p.rel, error.FileNotFound);
    if (st.kind != .directory) return storeFailed(run, pos, .walk, p.rel, error.NotDir);
    var levels: std.ArrayList(Level) = .empty;
    levels.append(a, .{ .rel = p.rel, .full = p.full, .entries = try sortedEntries(run, pos, .walk, p.full, p.rel), .depth = 1 }) catch return run.oom(pos);
    if (o.after) |after| try walkResume(run, pos, p, o, after, &levels);
    const out = try run.newTable(pos);
    out.kind = .list;
    var considered: u64 = 0;
    var last: []const u8 = "";
    while (levels.items.len > 0) {
        const top = &levels.items[levels.items.len - 1];
        if (top.at == top.entries.len) {
            _ = levels.pop();
            continue;
        }
        // At most `call_entries` per call, and none past `keep_reads`; with
        // more to come, the list ends with where to resume.
        const kept_back = if (o.keep_reads) |keep| run.budgets.reads -| run.reads < keep +| entry_reads else false;
        if (considered == run.budgets.call_entries or kept_back) {
            out.list.append(a, try moreEntry(run, pos, if (considered == 0) o.after orelse "" else last)) catch return run.oom(pos);
            break;
        }
        considered += 1;
        const en = top.entries[top.at];
        top.at += 1;
        if (o.skip) |pat| if (try text.likeCharged(run, pos, en.name, pat)) {
            last = try under(run, pos, top.rel, en.name);
            continue;
        };
        const depth = top.depth;
        const rel = try under(run, pos, top.rel, en.name);
        last = rel;
        try run.charge(pos, rel.len);
        run.tally(rel.len);
        const into = en.kind == .directory and (o.depth == null or depth < o.depth.?);
        const full: ?[]const u8 = if (en.kind != .other or into) std.fmt.allocPrint(a, "{s}/{s}", .{ top.full, en.name }) catch return run.oom(pos) else null;
        var keep = o.kind == null or o.kind.? == en.kind;
        if (keep) if (o.like) |pat| {
            keep = try text.likeCharged(run, pos, en.name, pat);
        };
        if (keep) {
            // Trust the listing's kind, never a stat's: a stat follows links,
            // so a symlink to `/` would walk the host's files and a link loop
            // would recurse. The listing calls a link `other`; it is reported,
            // never stat'ed or entered. Metal's FAT has no links, so both hosts
            // walk the same tree. Each stat counts as a read, as `stat(p)` does.
            const es: ?store.Stat = if (en.kind == .other) null else stat: {
                try countRead(run, pos);
                if (between_list_and_stat_for_test) |hook| hook();
                break :stat store.stat(run.io, a, full.?) catch |e| {
                    // A background run reads while others write: skip an
                    // entry removed since its folder was listed.
                    if (run.read_only and e == error.FileNotFound) continue;
                    return storeFailed(run, pos, .walk, rel, e);
                };
            };
            const m = try run.newTable(pos);
            m.map.put(a, "path", .{ .string = rel }) catch return run.oom(pos);
            m.map.put(a, "kind", .{ .string = @tagName(en.kind) }) catch return run.oom(pos);
            if (es == null) {
                m.map.put(a, "size", .{ .int = 0 }) catch return run.oom(pos);
                m.map.put(a, "modified", .{ .int = 0 }) catch return run.oom(pos);
            } else {
                const st_e = es.?;
                m.map.put(a, "size", .{ .int = if (en.kind == .directory) 0 else @intCast(st_e.size) }) catch return run.oom(pos);
                m.map.put(a, "modified", .{ .int = modifiedSeconds(st_e.mtime) }) catch return run.oom(pos);
            }
            m.kind = .map;
            out.list.append(a, .{ .table = m }) catch return run.oom(pos);
        }
        if (into) {
            try countRead(run, pos);
            // Likewise a folder removed since it was listed.
            const entries = (try sortedEntriesOrGone(run, pos, .walk, full.?, rel)) orelse continue;
            levels.append(a, .{ .rel = rel, .full = full.?, .entries = entries, .depth = depth + 1 }) catch return run.oom(pos);
        }
    }
    return .{ .table = out };
}

/// The last entry of a part when there is more: `{path, kind = "more"}`,
/// with `path` the place the next part resumes after.
fn moreEntry(run: *Run, pos: ast.Pos, path: []const u8) Error!Value {
    const a = run.alloc;
    const m = try run.newTable(pos);
    m.map.put(a, "path", .{ .string = path }) catch return run.oom(pos);
    m.map.put(a, "kind", .{ .string = "more" }) catch return run.oom(pos);
    m.map.put(a, "size", .{ .int = 0 }) catch return run.oom(pos);
    m.map.put(a, "modified", .{ .int = 0 }) catch return run.oom(pos);
    m.kind = .map;
    return .{ .table = m };
}
