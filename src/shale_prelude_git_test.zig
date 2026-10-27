//! Tests the git prelude on a temporary repo made with `git init` inside the
//! root. The reading calls go through the gate, each engine in its own copy of
//! the repo, since they take no lock and write nothing; the changing ones run
//! the VM once, as a commit cannot happen three times alike.

const std = @import("std");
const testing = std.testing;
const T = @import("shale_test_host.zig").T;
const host_linux = @import("shale_host_linux.zig");
const spawn = @import("shale_spawn.zig");

/// The environment for the repo's git: no user or system config, and a fixed
/// clock and name, so every engine's hashes are the same.
fn init(t: *T) !void {
    try t.init();
    t.preludes = &.{ host_linux.linux_prelude, host_linux.git_prelude };
    const a = t.arena.allocator();
    t.host.env = try a.dupe([]const u8, &.{
        t.path_env,                             "HOME=/nonexistent",
        "GIT_CONFIG_GLOBAL=/dev/null",          "GIT_CONFIG_NOSYSTEM=1",
        "GIT_AUTHOR_NAME=Test",                 "GIT_AUTHOR_EMAIL=test@example.com",
        "GIT_COMMITTER_NAME=Test",              "GIT_COMMITTER_EMAIL=test@example.com",
        "GIT_AUTHOR_DATE=2026-10-17T00:00:00Z", "GIT_COMMITTER_DATE=2026-10-17T00:00:00Z",
    });
}

const Collect = struct {
    buf: std.ArrayList(u8) = .empty,
    pub const Error = error{OutOfMemory};
    pub fn write(c: *Collect, bytes: []const u8) Collect.Error!void {
        try c.buf.appendSlice(testing.allocator, bytes);
    }
};

/// Runs git in `dir` under the root, outside any script.
fn git(t: *T, dir: []const u8, args: []const []const u8) !void {
    const a = t.arena.allocator();
    var argv: std.ArrayList([]const u8) = .empty;
    try argv.append(a, "git");
    try argv.appendSlice(a, args);
    var c: Collect = .{};
    defer c.buf.deinit(testing.allocator);
    const o = try spawn.run(a, .{ .argv = argv.items, .cwd = try std.fmt.allocPrint(a, "{s}/{s}", .{ t.root, dir }), .env = t.host.env, .timeout_ms = 20_000 }, &c);
    if (o.code != 0) {
        std.debug.print("git {s}: {s}\n", .{ args[0], c.buf.items });
        return error.TestUnexpectedResult;
    }
}

/// A repo at the root: one commit (a.txt, b.txt), then a.txt changed,
/// b.txt renamed d.txt and staged, c.txt untracked.
fn repo(t: *T) !void {
    try git(t, "", &.{ "init", "-q", "-b", "main" });
    try t.put("a.txt", "one\n");
    try t.put("b.txt", "bee\n");
    try git(t, "", &.{ "add", "a.txt", "b.txt" });
    try git(t, "", &.{ "commit", "-q", "-m", "first" });
    try t.put("a.txt", "one\ntwo\n");
    try git(t, "", &.{ "mv", "b.txt", "d.txt" });
    try t.put("c.txt", "see\n");
}

fn expectOnce(t: *T, src: []const u8, want: []const u8) !void {
    var r = t.runOnce(src, .atomic);
    defer r.deinit();
    if (r.stop) |s| {
        std.debug.print("{s}\n  stopped {d}:{d} {s}: {s}\n", .{ src, s.line, s.column, @tagName(s.class), s.message });
        return error.TestUnexpectedResult;
    }
    try testing.expectEqualStrings(want, r.output);
}

test "git prelude: the reading calls give data" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try repo(&t);
    try t.expectOutput("for e in git_status(\"\") do print(e.code, e.path, get(e, \"from\", \"-\")) end", " M\ta.txt\t-\nR \td.txt\tb.txt\n??\tc.txt\t-\n");
    try t.expectOutput("print(git_is_clean(\"\"), #git_head(\"\"), join(git_ls_files(\"\"), \",\"))", "false\t7\ta.txt,d.txt\n");
    try t.expectOutput("local l = git_log(\"\", {n = 1}) print(#l, l[1].subject, l[1].author, l[1].date, l[1].hash == git_head(\"\"))", "1\tfirst\tTest\t2026-10-17T00:00:00+00:00\ttrue\n");
    try t.expectOutput("local l = git_log(\"\", {fields = {\"subject\"}}) print(#l, has_key(l[1], \"hash\"), l[1].subject)", "1\tfalse\tfirst\n");
    try t.expectOutput("print(git_show(\"\", \"HEAD:a.txt\"))", "one\n\n");
    // `git diff` refreshes the index's stat data even without the lock
    // (git 2.43), so each gate copy's index would differ: run once.
    try expectOnce(&t, "print(contains(git_diff(\"\", {}), \"+two\"), contains(git_diff(\"\", {stat = true}), \"a.txt | 1 +\"), git_diff(\"\", {paths = {\"d.txt\"}}) == \"\")", "true\ttrue\ttrue\n");
    try expectOnce(&t, "print(contains(git_diff(\"\", {range = \"HEAD\", paths = {\"a.txt\"}}), \"+two\"))", "true\n");
}

test "git prelude: a git error stops, naming the call and git's words; an option it does not take, by name" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try repo(&t);
    try t.expectStop("git_show(\"\", \"HEAD:nope\")", .atomic, .fail, "`git show HEAD:nope` exited 128: fatal: path 'nope' does not exist in 'HEAD'");
    try t.expectStop("git_log(\"\", {count = 1})", .atomic, .fail, "`git_log` takes n, range, ref, fields and timeout_ms, not `count`");
    try t.expectStop("git_status(\"nowhere\")", .atomic, .fail, "`git status");
    // A repo that is not a path, and `all` for `add = "all"`, are named.
    try t.expectStop("git_status({\"x\"})", .atomic, .fail, "`git status --porcelain=v1 -z`: its repo is a path (a string)");
    try t.expectStop("git_commit(\"\", \"m\", {all = true})", .atomic, .fail, "`git_commit` takes `add = \"all\"` for every change, not `all`");
}

test "git prelude: commit by tracked files, by all, by paths; a failed one stops" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try repo(&t);
    try expectOnce(&t, "git_commit(\"\", \"second\", {add = \"tracked\"}) print(git_log(\"\", {n = 1})[1].subject, #git_status(\"\"))", "second\t1\n");
    try expectOnce(&t, "git_commit(\"\", \"third\\n\\nits body\", {add = \"all\"}) print(git_log(\"\", {n = 1})[1].subject, git_is_clean(\"\"))", "third\ttrue\n");
    try t.put("e.txt", "e\n");
    try t.put("f.txt", "f\n");
    try expectOnce(&t, "git_commit(\"\", \"fourth\", {add = {\"e.txt\"}}) print(join(git_ls_files(\"\"), \",\"), git_status(\"\")[1].path)", "a.txt,c.txt,d.txt,e.txt\tf.txt\n");
    var bad = t.runOnce("git_commit(\"\", \"x\", {add = \"everything\"})", .atomic);
    defer bad.deinit();
    try testing.expectEqualStrings("`git_commit`'s `add` is \"tracked\", \"all\" or a list of paths, not \"everything\"", bad.stop.?.message);
    var r = t.runOnce("git_commit(\"\", \"empty\", {add = \"tracked\"})", .atomic);
    defer r.deinit();
    try testing.expect(std.mem.indexOf(u8, r.stop.?.message, "`git commit -q -a -F -` exited 1") != null);
}

test "git prelude: branches, worktrees, and a remote: push, fetch, pull and merge fast-forward" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try repo(&t);
    try git(&t, "", &.{ "commit", "-q", "-a", "-m", "second" });
    try t.tmp.dir.createDirPath(t.threaded.io(), "remote.git");
    try git(&t, "remote.git", &.{ "init", "-q", "--bare", "-b", "main" });
    try git(&t, "", &.{ "remote", "add", "origin", try std.fmt.allocPrint(t.arena.allocator(), "{s}/remote.git", .{t.root}) });
    try expectOnce(&t, "git_push(\"\", {remote = \"origin\", branch = \"main\"}) git_fetch(\"\", {all = true}) print(git_log(\"\", {n = 1, ref = \"origin/main\"})[1].subject)", "second\n");
    try expectOnce(&t, "git_worktree_add(\"\", \"wt\", {branch = \"feat\", new = true}) print(read(\"wt/a.txt\"))", "one\ntwo\n\n");
    try t.put("wt/g.txt", "g\n");
    try expectOnce(&t, "git_commit(\"wt\", \"on feat\", {add = \"all\"}) git_merge_ff(\"\", \"feat\") print(git_log(\"\", {n = 1})[1].subject)", "on feat\n");
    try expectOnce(&t, "git_worktree_remove(\"\", \"wt\", {force = true}) git_branch_delete(\"\", \"feat\", {}) print(exists(\"wt\"))", "false\n");
    try expectOnce(&t, "git_push(\"\", {}) git_pull_ff(\"\") print(git_head(\"\") == git_log(\"\", {n = 1, ref = \"origin/main\"})[1].hash)", "true\n");
}

test "git prelude: the upstream's counts; an amend before a push, refused after; a tag pushed alone; a worktree at a rev" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try repo(&t);
    try git(&t, "", &.{ "commit", "-q", "-a", "-m", "second" });
    try t.tmp.dir.createDirPath(t.threaded.io(), "remote.git");
    try git(&t, "remote.git", &.{ "init", "-q", "--bare", "-b", "main" });
    try git(&t, "", &.{ "remote", "add", "origin", try std.fmt.allocPrint(t.arena.allocator(), "{s}/remote.git", .{t.root}) });
    try expectOnce(&t, "print(git_upstream(\"\") == nil)", "true\n");
    try expectOnce(&t, "git_push(\"\", {remote = \"origin\", branch = \"main\"}) local u = git_upstream(\"\") print(u.name, u.ahead, u.behind)", "origin/main\t0\t0\n");
    try t.put("a.txt", "three\n");
    try expectOnce(&t, "git_commit(\"\", \"third\", {add = \"tracked\"}) git_commit(\"\", \"third, said again\", {add = \"tracked\", amend = true}) local l = git_log(\"\", {n = 2}) print(l[1].subject, l[2].subject, git_upstream(\"\").ahead)", "third, said again\tsecond\t1\n");
    try expectOnce(&t, "git_push(\"\", {})", "");
    var pushed = t.runOnce("git_commit(\"\", \"x\", {amend = true})", .atomic);
    defer pushed.deinit();
    try testing.expectEqualStrings("`git_commit`'s `amend`: HEAD is on origin/main already; commit anew", pushed.stop.?.message);
    try expectOnce(&t, "git_tag(\"\", \"v1\", {msg = \"the first\\n\\nits body\"}) git_tag(\"\", \"light\", {rev = \"HEAD~1\"}) git_push(\"\", {tag = \"v1\"}) local there = must(run({\"git\", \"-C\", real(\"remote.git\"), \"tag\", \"-n1\"}, {timeout_ms = 10000, whole = 10000})).out print(contains(there, \"v1\"), contains(there, \"the first\"), contains(there, \"light\"))", "true\ttrue\tfalse\n");
    try t.expectStop("git_push(\"\", {tag = \"v1\", branch = \"main\"})", .atomic, .fail, "`git_push` takes a `branch` or a `tag`, not both");
    try expectOnce(&t, "git_worktree_add(\"\", \"old\", {rev = \"HEAD~1\"}) print(read(\"old/a.txt\"), git_head(\"old\") == git_log(\"\", {n = 1, ref = \"HEAD~1\"})[1].hash) git_worktree_remove(\"\", \"old\", {})", "one\ntwo\n\ttrue\n");
}

test "git prelude: restore puts paths back as HEAD or a rev has them, index and tree; mv moves a tracked path, by case alone too" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try repo(&t);
    // a.txt changed and staged: both undone; the rest left as they were.
    try git(&t, "", &.{ "add", "a.txt" });
    try expectOnce(&t, "git_restore(\"\", {\"a.txt\"}) print(read(\"a.txt\")) for e in git_status(\"\") do print(e.code, e.path) end", "one\n\nR \td.txt\n??\tc.txt\n");
    try git(&t, "", &.{ "add", "-A" });
    try git(&t, "", &.{ "commit", "-q", "-m", "second" });
    try t.put("a.txt", "changed\n");
    try git(&t, "", &.{ "commit", "-q", "-a", "-m", "third" });
    try expectOnce(&t, "git_restore(\"\", {\"a.txt\"}, {from = \"HEAD~1\"}) print(read(\"a.txt\"), git_status(\"\")[1].code)", "one\n\tM \n");
    try t.expectStop("git_restore(\"\", {\"nope.txt\"})", .atomic, .fail, "`git restore --source=HEAD --staged --worktree -- nope.txt` exited");
    try t.expectStop("git_restore(\"\", {})", .atomic, .fail, "`git_restore` of no paths");
    try git(&t, "", &.{ "commit", "-q", "-a", "-m", "fourth" });
    try t.put("Word.roc", "w\n");
    try git(&t, "", &.{ "add", "Word.roc" });
    try git(&t, "", &.{ "commit", "-q", "-m", "fifth" });
    try expectOnce(&t, "git_mv(\"\", \"Word.roc\", \"word.roc\") local s = git_status(\"\") local names = join(ls(\"\"), \",\") print(contains(names, \"word.roc\"), contains(names, \"Word.roc\"), s[1].code, s[1].path, s[1].from)", "true\tfalse\tR \tword.roc\tWord.roc\n");
}

test "git prelude: a merge makes its commit; a conflicted one is undone and named; \".\" is the root" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try repo(&t);
    try git(&t, "", &.{ "add", "-A" });
    try git(&t, "", &.{ "commit", "-q", "-m", "second" });
    try t.expectOutput("print(git_head(\".\") == git_head(\"\"))", "true\n");
    // Apart on two files: a merge commit.
    try git(&t, "", &.{ "branch", "side" });
    try t.put("h.txt", "h\n");
    try git(&t, "", &.{ "add", "h.txt" });
    try git(&t, "", &.{ "commit", "-q", "-m", "main's" });
    try git(&t, "", &.{ "checkout", "-q", "side" });
    try t.put("i.txt", "i\n");
    try git(&t, "", &.{ "add", "i.txt" });
    try git(&t, "", &.{ "commit", "-q", "-m", "side's" });
    try git(&t, "", &.{ "checkout", "-q", "main" });
    try expectOnce(&t, "git_merge(\".\", \"side\") print(git_log(\"\", {n = 1})[1].subject, exists(\"i.txt\"))", "Merge branch 'side'\ttrue\n");
    // Both change a.txt: undone, named, the tree clean.
    try git(&t, "", &.{ "checkout", "-q", "-b", "other" });
    try t.put("a.txt", "theirs\n");
    try git(&t, "", &.{ "commit", "-q", "-a", "-m", "other's" });
    try git(&t, "", &.{ "checkout", "-q", "main" });
    try t.put("a.txt", "ours\n");
    try git(&t, "", &.{ "commit", "-q", "-a", "-m", "ours" });
    var r = t.runOnce("git_merge(\"\", \"other\")", .atomic);
    defer r.deinit();
    try testing.expectEqualStrings("`git merge other`: conflicts in a.txt; the merge is undone", r.stop.?.message);
    try expectOnce(&t, "print(git_is_clean(\"\"), read(\"a.txt\"))", "true\tours\n\n");
}

test "git prelude: SHALE-AGENTS.md's user-prelude commit refuses a message without its attribution" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try repo(&t);
    // The doc as it stands, read where the build runs the tests.
    const doc = try std.Io.Dir.cwd().readFileAlloc(t.threaded.io(), "docs/SHALE-AGENTS.md", t.arena.allocator(), .limited(1 << 20));
    const start = std.mem.indexOf(u8, doc, "function commit(").?;
    const end = std.mem.indexOfPos(u8, doc, start, "\nend\n").? + "\nend\n".len;
    t.preludes = &.{ host_linux.linux_prelude, host_linux.git_prelude, .{ .name = "the user prelude", .source = doc[start..end] } };
    var bad = t.runOnce("commit(\"\", \"no attribution\", {add = \"all\"})", .atomic);
    defer bad.deinit();
    try testing.expectEqualStrings("`commit`: the message has no Co-Authored-By: line", bad.stop.?.message);
    try expectOnce(&t, "commit(\"\", \"signed\\n\\nCo-Authored-By: Test <t@x>\", {add = \"all\"}) print(git_log(\"\", {n = 1})[1].subject)", "signed\n");
}

/// A hook of the repo at the root: `name`, a shell script that runs `body`.
fn hook(t: *T, name: []const u8, body: []const u8) !void {
    const a = t.arena.allocator();
    const rel = try std.fmt.allocPrint(a, ".git/hooks/{s}", .{name});
    try t.put(rel, try std.fmt.allocPrint(a, "#!/bin/sh\n{s}\n", .{body}));
    var c: Collect = .{};
    defer c.buf.deinit(testing.allocator);
    const o = try spawn.run(a, .{ .argv = &.{ "chmod", "+x", try std.fmt.allocPrint(a, "{s}/{s}", .{ t.root, rel }) }, .env = t.host.env, .timeout_ms = 20_000 }, &c);
    try testing.expectEqual(@as(?u8, 0), o.code);
}

test "git prelude: a git that runs out of time, or is ended by a signal, stops saying which; a merge that fails without a conflict, by its exit or its time" {
    var t: T = undefined;
    try init(&t);
    defer t.deinit();
    try repo(&t);
    // A commit whose hook outlasts the call's time.
    try hook(&t, "pre-commit", "sleep 5");
    var slow = t.runOnce("git_commit(\"\", \"x\", {add = \"all\", timeout_ms = 300})", .atomic);
    defer slow.deinit();
    try testing.expectEqualStrings("`git commit -q -F -` timed out", slow.stop.?.message);
    // One whose hook ends the git that runs it.
    try hook(&t, "pre-commit", "kill -9 $PPID");
    var killed = t.runOnce("git_commit(\"\", \"x\", {add = \"all\"})", .atomic);
    defer killed.deinit();
    try testing.expectEqualStrings("`git commit -q -F -` was ended by a signal", killed.stop.?.message);
    try hook(&t, "pre-commit", "exit 0");
    try git(&t, "", &.{ "commit", "-q", "-m", "second" });
    // A ref that is none: git's words, then its exit.
    var none = t.runOnce("git_merge(\"\", \"nosuch\")", .atomic);
    defer none.deinit();
    try testing.expectEqualStrings("`git merge nosuch` exited 1", none.stop.?.message);
    try testing.expect(std.mem.indexOf(u8, none.output, "nosuch") != null);
    // A merge that makes a commit, whose hook outlasts the merge's time.
    try git(&t, "", &.{ "branch", "side" });
    try t.put("h.txt", "h\n");
    try git(&t, "", &.{ "add", "h.txt" });
    try git(&t, "", &.{ "commit", "-q", "-m", "main's" });
    try git(&t, "", &.{ "checkout", "-q", "side" });
    try t.put("i.txt", "i\n");
    try git(&t, "", &.{ "add", "i.txt" });
    try git(&t, "", &.{ "commit", "-q", "-m", "side's" });
    try git(&t, "", &.{ "checkout", "-q", "main" });
    try hook(&t, "pre-merge-commit", "sleep 5");
    var stuck = t.runOnce("git_merge(\"\", \"side\", {timeout_ms = 300})", .atomic);
    defer stuck.deinit();
    try testing.expectEqualStrings("`git merge side` did not finish (timeout)", stuck.stop.?.message);
}
