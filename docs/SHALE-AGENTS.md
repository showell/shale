# Shale as an agent's tool

**Shale was made for a site's own data on bare metal, and what it does
there to stay safe is what an agent wants on Linux too:**

- **Every run is bounded.** Its steps, its reads, its changes and **what
  it prints** each have a budget (`--budget NAME=N` sets one; a script
  that needs more says so in a `-- budget: NAME=N ...` line among its
  opening comments, which `shale` takes and prints). A script
  that would print a megabyte stops at the output budget (64 KiB in `shale` unset)
  and says so: its output is a bounded context cost, never a flood.
- **A stop is one line that says where and why**, with the calls that led
  there (`called from file:line:column (in f)`), and a class: `type`,
  `index`, `file_system`, `budget reads`. No traceback to read through.
- **A run makes its changes; git is the undo.** There is no dry run:
  every change is checked and logged, an `edit`'s
  diff is printed before it is written, and an edit that would not pin its
  text down is refused before anything is changed. Work in a checkout, so
  `git diff` shows what a run did and `git checkout --` takes it back.
- **It reads only what it is rooted at** (`--root`, else `SHALE_ROOT`, else
  the working directory) and its mounts (`runs:x`): no `..`, no absolute paths. It
  reaches past them only by running a program (`run`). **A root is never
  the home:** one that is the home folder, holds `~/.ssh`, `~/.config` or
  `~/.claude`, or lies inside `~/.ssh` or `~/.config` is refused before the
  run starts (the list of those folders is in `c/host/mounts.c`); run from
  `~/showell_repos`, a folder below it, or a mount's folder. The gate
  passes a `cd DIR && ` into those alone (its rule is in
  `c/gate/gate.c`): an absolute path, `~/...`, or one from the shell's folder.

`shale` is terse: past half of any budget, or on a stop, it says what the
run used of four, steps, reads, read bytes and output bytes (`── used:
52374 of 5000000 steps, 12 of 10000 reads, ...`), so a script near an edge
is known before a bigger run finds it;
`--verbose` says so always, with the effects and the programs run. **Below
it, the `── steps:` line says where the steps went**, in the time line's
shape: the three that spent the most, with their shares and how often
script code called them (`── steps: 77.3M in all; the most 41.2M (53%):
re_find_all, 2,310 calls; 12.0M (16%): lines, 40 calls; 10.1M (13%):
script`). A builtin's steps are its own, and so are a function's of
Shale's own preludes (`grep_tree`, `run`, `create`), with every builtin it
calls; `script` is the script's own code, its functions and libraries
included, and what a builtin they call spends is that builtin's. So when a
run nears its step budget, the line says which call to look at, with no
profiling script. (A run that stops before it starts, a parse's stop, ran
nothing and has none.) A run
that spent a second or more in programs, job waits and sleeps says where,
the slowest three programs named with their shares of it (`── time: 3
programs, 52.1 s in all; the slowest 51.8 s (99%): roc build ...; 0.2 s
(0%): git log; 0.1 s (0%): git status`): when a step is slow, that line
says which program to look at. The
arena is not among the four: a stop on it (`budget arena`, 16 MiB unset)
says nothing of what it would have needed: raise it and run again
(`--budget arena_bytes=4000000000`), and if the script is one you keep,
put it in its `-- budget:` line.

**Slowness is a finding.** We want things to be fast 90% of the time, so
don't tolerate slowness. A loop over about 10 seconds is a finding to
explain by phase, never a wait, and a run over budget isn't done. Find
which phase took the time and fix that; a budget moves only once you can
say why the stage's real cost changed.

**A stage states what it may cost**, in CPU time, because wall time doubles
on a busy box and would make a budget flaky. `run`'s `cpu_budget_ms = N`
stops the run when the program and the children it waited for use more
than N ms of CPU, naming the stage (`stage = "..."`, else the argv), the
budget and what it took, as `budget cpu`, which `attempt` cannot catch.
At N exactly it passes. `run`'s answer has `cpu_ms` beside `ms` whether or
not a budget is set. **`cookbook/times.shale` compares a stage with its
recent runs**, so a slowdown shows with no budget set:

```lua
use("cookbook:times.shale")
local r = must(timed_run("safari", "build.sh safari", {"./build.sh", "safari"},
  {timeout_ms = 300000, tail = 20, cpu_budget_ms = 60000}))
-- build.sh safari: 192 s, was 35 s (5.5x)
```

The first word names a `.tsv` file under `build:times`, one a script, which
holds each stage's last five runs. The line compares CPU time with the
middle of those runs, and appears at 1.5x and a second of change, either
way (`(5.5x faster)`). A run that did not exit 0 counts for nothing, and a
stage over its budget stops in `run`, so neither moves the history.
**`cold = true`** says the run compiles from nothing, as a fresh worktree's
first does: it isn't held to `cpu_budget_ms` or compared, and stays out of
the history (`page: cold, 192 s, not judged`), so a tier can keep
a budget. The caller says so, from its own cache, read once before its first
stage: a book outlives a deleted cache, and two worktrees share one.
`build:` has to be writable: a read-only one stops `timed_run` before the
program starts.

**On the box, a budget of steps, reads or bytes is not sacred.** A time
budget is. Those are there for Shale on
bare metal, where a runaway script takes a site's server down, and there
they hold. A refactoring or lint tool on the box (the cookbook's recipes, a
one-off hunt or rewrite) may raise steps, reads and read bytes as far as its
job needs: a stop on one says how big the job is, not that it is wrong.
So may `call_depth` (64 unset), for a recursion over a deep tree: the Zig
stack's guard is `depth`, which no command line sets. What still binds is
the machine itself:
- **memory**: keep `arena_bytes` well under the RAM that is free (`free -m`;
  this box has 8 GB and no swap, and a 12 GB arena hung it on 2026-11-01);
- **CPU**: run heavy jobs one at a time, never beside a timing gate;
- **one huge input**: split it into chunks, each its own run, rather than
  holding it all at once.

The output budget is a different case: it guards your context, so raise it
for a run whose output goes to a file, not to your console. An
`edit`'s diff is one line, `edit p: 3 hunks, -4 +5 lines`, unless
`--verbose`, which prints it in full. With
no file and no `-e`, the script is stdin (`shale <<'SHALE'`).

**A recipe with values a plain word cannot carry** (a doc, a test's name,
`|`): its values on stdin, one a line, raw, `shale ~/r/split.shale
--args-stdin <<'ARGS'` then `doc=It's "the" doc` and `names=a|b`. **A
script that changes several files**: `--undo-on-stop`, and a stop puts
back what it changed, so no half-done edit is left (SHALE.md, "The
runner": what it cannot put back, it says).

```sh
shale c/install.shale              # from Shale's checkout: builds C Shale, checks it, puts it on PATH
shale --version                    # says the mode: ReleaseSafe for the install, Debug for a tier's build
shale count.shale dir=src
shale rename.shale from=old to=new
shale <<'EOF'
for p in glob("src/*.zig") do print(p) end
EOF
```

(`-e SOURCE` runs a script given on the command line too, but the gate
refuses it: its quotes and `*` are not plain words. It is for a shell
outside the gate.)

A value given as `name=value` is the script's `arg("name", default)`; a
default that is an integer or a boolean reads the value as one. **A value
no `arg` call reads stops the run before it starts**, naming the ones the
script reads (`cwd=` given to a recipe that reads `dir=`), unless the
script names one by a value made as it runs, which cannot be checked.

## Before you reach for the shell

**Most escapes past the gate are a shell habit for something Shale already
does** (`grep -n`, `sed -n a,bp`, `git` and `zig build | grep` above all).
Each has a Shale form, most of them one launch:

| the habit | in Shale |
|---|---|
| `sed -n 40,80p f; sed -n 5,9p g` | `shale cookbook:look.shale at=f:40-80,g:5-9` |
| `cp -r site/live/markdown dev/` | `copy("site/live/markdown", "build:roc-apps/next/markdown")`: a file or a tree, across mounts, never over a path that is already there, and a symbolic link in it stops the copy before anything is written; the answer is how many files |
| `pkill -f pat`, `ps aux \| grep pat \| xargs kill` | `for p in procs() do if find(p.args, "pat") ~= nil then proc_stop(p) end end`: never this run or its shell |
| `grep -rn 'text' src build.zig` | `shale cookbook:look.shale find=text in=src,build.zig ext=zig` (a value with `(`, `*` or a space: `--args-stdin`) |
| `run({"grep", "-rn", ...})` inside a heredoc, refused even with a want | `for h in grep_tree("src", "text", {ext = {"zig"}}).hits do print(h.path .. ":" .. h.line .. ": " .. h.text) end` (`regex = true`, `nocase = true`, `context = 2` for `-A2 -B2`); one file: `grep(p, "text")`, `re_lines(read(p), pattern)`, `grep_all(p, "text")` past the call-bytes cap, `#grep(p, "text")` a count |
| `run({"sed", "-i", ...})` inside a heredoc, refused even with a want | `edit(p, old, new)`: a literal, stopping the run when `old` isn't there, so no edit misses quietly; `re_replace(s, pattern, new)` then `replace(p, s)` for a regex |
| `grep -rn 'a phrase' docs` over prose, which misses one a line break splits | `shale cookbook:find_phrase.shale find="a phrase" in=docs` (phrase on stdin with `--args-stdin`): each paragraph, list item, quote and comment block read whole (never a code fence), so a break, an indent or a run of spaces inside the phrase is one space; a hit says `path:line` where it starts, `(N lines)` when it crosses breaks, `(markup inside)` when only `*` or a backtick splits it; `new=` shows the edit and `write=1` makes it, rewrapping only the lines the edit moves |
| grep in Zig's std, a sibling repo | the same, through a mount: `in=zig:lib/std/Build.zig`, `repos:roc-chat/app` |
| `zig build test 2>&1 \| grep error \| sort \| uniq -c` | `shale cookbook:run_zig_build.shale step=test` (`zig=` another compiler, through a `tools` mount): the summary, each error once with its count, failed tests by name, the whole output kept in a log |
| `zig build fuzz 2>&1 \| grep FAIL` | the same with `grep=FAIL,hunts ran` (a value with a space: on stdin, `--args-stdin`): every output line holding one of the literals, after the summary |
| `roc check f && roc test f 2>&1 \| tail`, any other program and its verdict | `shale cookbook:run.shale` with `cmd=roc check f && roc test f` on stdin (`--args-stdin`): each command in order until one fails, how it ended and its time, its last lines (`tail=`), lines holding `grep=` literals, the whole output in .zig-cache/run.log where that folder is (else `log=PATH`, or not kept, said); `want=tag: why` for a program `run` refuses (`node`, `sh`); inside a heredoc, `report_run(argv, {grep, tail, keep_going})`, the same verdict with no child run |
| `python3 -m py_compile f`, `node --check f`, `zig ast-check f`, `bash -n f`: did an edit's syntax survive? | `shale cookbook:check_syntax.shale files=f,g` (or `rev=HEAD~1..HEAD`): one verdict a file by its kind (`.zig`, `.zon`, `.roc`, `.js`, `.mjs`, `.shale`, `.py`, `.sh`), then PASS or FAIL |
| `grep -rn ".field =" src`: does anything but its own file make a sealed value (a checked id, a path)? | `shale cookbook:check_seals.shale dir=src seals=path_seal:src/script/builtins_store.zig` (several trees: `dir=a,b`; a header only one file sets: `headers=location:<file>`): Zig's tokens, so a comment or a string never counts; one line a forger, then PASS or FAIL |
| `curl -s URL \| jq .messages[0].id`, an API's JSON | in a heredoc, `json_read(http_get(url).body).messages[1].id` (an object a map, an array a list, from 1); `json_write(v)` back |
| `curl -s URL \| grep X`, a wait for a deploy | `shale cookbook:fetch_page.shale url=URL grep=X` (`wait=LITERAL minutes=N`: until the body holds it; `bearer=` a Shale path to a token file) |
| `curl -u email:KEY` a Zulip topic, or any API wanting basic auth from a secret | `shale cookbook:read_zulip_topic.shale --args-stdin` (`site=`, `email=`, `key=keys:<file>`, `stream=`, `topic=`, `num=`): the key reaches curl alone, by its environment (`--variable`/`--expand-user`), as `run`'s `secrets` |
| `curl -sN .../stream \| grep @CC`, hearing a roc-chat post meant for you | `shale cookbook:listen.shale site=https://chat.lynrummy.com conv=3 me=agent:2 key=keys:<file> once=1` (`after=` the last id handled; without `once=1`, `log=` a file each line is appended to): one line a post for the agent, `#<id> by <principal>: <first line>`. A post is for it when the server's `mentions` name the agent (roc-chat decides who a post names, skipping quoted and fenced text) and is not its own; no rule limits agents answering agents, as the roc-chat server bounds agent-to-agent traffic (15 an hour since a human post); at 12 of the agent's own it prints one `budget:` line, once a crossing, and runs on. The stream is held in windows of `window=` seconds (20), so a post is heard at most a window late. It reconnects by itself, so no retry loop of your own: an ended stream (a deploy restarts the server), no connection or any status but 401, 403 and 404 prints `reconnecting after <reason>, from after=<id>, in <n> s`, waits (2, 5, 15, 30, then 60 s) and resumes, nothing missed or heard twice. A 401 or 403 (the key revoked or wrong), a 404 (no such conversation), a gap or a message without `mentions` (a server older than them) stops it nonzero, named; the windows run in batches, each a child run of the recipe with its own 63 curls (windows and reconnects together, about 21 minutes of quiet at 20 s windows), one after another from the last id, so a run listens through about 22 hours of quiet (63 batches) before it stops naming the `after=` to start it again with; a batch's lines reach the run's output when the batch ends (with `once=1`, at the post), and `log=` as heard. **`conv=all`** listens on the agent's one stream (roc-chat's `GET /stream`), every conversation it is in and every channel topic that names it: a post is `#<id> in conversation <c> by <principal>: <first line>`, or `#<id> in topic <channel>/<topic> by ...`, `after=` is the last event's place (`<at>.<spot>.<message>`, the spot a conversation's number or `<channel key>/<topic key>`, which the stop names; none or `0` is live only), a repeat the server's resume sends is heard once, and the budget warning is per conversation |
| `curl -X POST --data ... -c jar -b jar`, a response's headers | in a heredoc, `http("POST", url, {body, headers})`: `{status, headers, body}`, the headers by lowercased name (`r.headers["set-cookie"]`), a cookie sent back as a `"Cookie: ..."` header |
| `git tag -a v6 X -m ... && git push origin v6`, `git commit --amend`, ahead/behind | `git_tag(repo, "v6", {rev, msg})`, `git_push(repo, {tag = "v6"})`; `git_commit(repo, msg, {amend = true})`; `git_upstream(repo)` |
| `git checkout -- f`, `git mv a b` | `git_restore(repo, {"f"})`, `git_mv(repo, "a", "b")` |
| `git show origin/master:QUEUE.md \| sed -n 40,80p`, `git grep` at a revision | `shale cookbook:look.shale rev=origin/master at=QUEUE.md:40-80` (or `find=` `in=`): `at` and `in` read as the revision holds them (`repo=` another checkout) |
| `git log`, `git diff --stat`, `git show rev:path`, `git status` | the git prelude: `git_log`, `git_diff`, `git_show`, `git_status`, `git_is_clean` (below) |
| `git commit -m ... && git push` | `git_commit(repo, msg, {add})`, `git_push(repo)`; `git_worktree_add`, `git_merge_ff` |
| `git push origin HEAD:main`, `git push origin --delete lane`, `git push --force-with-lease` | `git_push(repo, {to = "main"})`, `git_push(repo, {delete = "lane"})`, `git_push(repo, {lease = true})`: a plain `run` of `git push` over ssh cannot read the key |
| `git fetch --prune origin`, `git pull --ff-only` | `git_fetch(repo, {remote = "origin", prune = true})`, `git_pull_ff(repo)`: a plain `run` of `git fetch` over ssh cannot read the key either |
| a script you would write in python | a heredoc, `shale <<'TAG'`: `edit`'s list form, `walk_all`, `grep`, `re_lines`, `run` |

A script is named through a mount too (`shale cookbook:look.shale`, once
`~/.config/shale/mounts` has a `cookbook = /ABSOLUTE/PATH/TO/shale/cookbook`
line), so a recipe launches from any checkout. The box's mounts are in
`~/.config/shale/mounts`; `shale` says which it read when a name is missing.
Escape only for what this table and the cookbook don't cover (a build run in
the background, an interactive tool, a path no mount reaches), and say what
was missing in the escape's text: that is how the next want is found.

**What it has for code:** `glob(pattern)` (`**` any folders, `*` and `?`
within one; hidden names like `.git` and `.zig-cache` left out, unless `{hidden = true}`),
`grep(p, s)` (the lines holding a literal, numbered), `read_lines` and
`count_lines` for a file of any size, `re_find(s, pattern)`, `re_match`
and `re_find_all(s, pattern)` (regular expressions, a linear-time subset:
`|`, `\b`, up to nine capture groups and `(?:...)`, `{nocase = true}`;
`re_find_all` gives each match as `re_find` does, and `re_find`'s own
`{from = n}` starts a search there), `between(s, a, b)` (the text after one string and before the
next), `edit(p, old, new[, {all, word}])` (a literal replaced, its diff
printed) and `edit(p, {{old = ..., new = ...}, ...})` (several in sequence,
one diff, one write, none made if any fails), long strings (`[=[ ... ]=]`,
raw, for text that holds quotes and lines), `walk` with `skip`, and the
language's own `split`, `words`, `contains`, `sort`, `table` and maps. The
prelude adds the small helpers scripts kept writing out: `slice(l, from,
to)`, `push_at(m, k, v)` (a map of lists), `incr(m, k[, n])`,
`set_of(list)` and `has_item(list, x)`, `find_all(s, t)`,
`dirname(p)`, `path_normal(p)`
(`a/./b/../c` is `a/c`) and `opts_only(opts, {"a", "b"}, who)` (a stop on
any other key). **Already there, and still rewritten:** `drop(l, n)`,
`basename(p)`, `find(s, t[, from])`, `fresh_dir(parent, prefix)`, and
`mkdir`, which makes the parents. The
whole language is [`SHALE.md`](SHALE.md).

**On Linux, the Linux prelude** (`c/preludes/linux.shale`; metal
has none of it):
- **Programs:** `run(argv, opts)` runs one and gives `{argv, code, ended,
  ms, cpu_ms, out, ...}`. `timeout_ms` and a keep, `head` or `tail` (lines)
  or `whole` (bytes), are required (unless `stdout_secret`); the rest are
  `cwd`, `stdin`, `env`, `secrets`, `secret_args`, `stdout_secret`, `secret_replace`,
  `log`, `want`, `cpu_budget_ms`, `stage`, `nice`, and the filters `re`, `not_re`, `nocase`,
  `before`, `after` and `width` (`RunOpts` in `c/host/host_run.c`).
  **The filters cost the script's steps:** each output line's search for
  `re` or `not_re` is charged to the run's step budget, so a pattern over a
  megabyte of output can stop the run on `budget steps` after the program
  has ended (the `── steps:` line puts it under `run`). A pattern that
  begins with a byte rare in the output is cheapest, as with `re_lines`:
  over `seq 1 200000`, `re = "x"` costs about 0.3M steps and `re =
  "(1|2|3)+9$"` about 9.4M, past the 5M a run has unset.
  **`nice = 19`** (0 to 19) runs a heavy job at the lowest priority,
  set in the child before it starts, so it needs no `nice` program (which
  `run` refuses) and no `want`; a `shale` already nicer than asked runs it
  at its own.
  **A line longer than `width` keeps both ends**, its middle said as `… N
  bytes left out …` (`c/host/keep.c`'s `keep_cut`): a child `shale`'s stop
  says where at its start and why at its end, and a cut at the end alone
  hid the why. Shale itself never cuts a stop, nor `attempt`'s `error`.
  **`width` is 4096 bytes unset**, so only a runaway line (minified code,
  a blob) is cut: it bounds the keep's memory (a kept line takes at most
  about twice it, so `tail = 1000` fits the arena), while the output
  budget already bounds what reaches your context. It was 200, which cut
  a test's PASS line and a child's stop. Its
  `cwd` is a path as every path is: under the root, or through a mount
  (`cwd = "repos:shale"` runs a sibling repo's step from another's
  checkout); `..` and an absolute path are refused. **What `run` refuses**
  unless its options say why (`want = "tag: why"`, logged in the run's
  effects): a shell or an interpreter (`bash`, `sh`, `python3`, `node`,
  `perl`: Shale is the script), a text tool (`grep`, `sed`, `cat`, `head`,
  ...: the builtins do it), a wrapper (`env`, `timeout`, `xargs`, ...: run
  the program itself), and a program by a path git does not track, unless
  it is under a ` tools` mount (a toolchain's install). A program is
  refused by its own name and by the name of the file it reaches, every
  link followed (`tool`, which is `bash`). An `env` that sets PATH, an
  `LD_` name, BASH_ENV or ENV needs a want too: each picks what runs.
  **A program runs confined to the mounts** (Landlock, Linux 6.2 or
  later; `c/host/host_confine.c` holds the list): it may write only under
  the root and the mounts without ` ro`; it may read and run only those, the
  read-only mounts and the system's folders (`/usr`, `/etc`, `/proc`,
  `/sys`, `/bin`, `/lib*`, the resolver's `/run/systemd/resolve`), plus
  `/dev/null`, `/dev/zero`, `/dev/full`, the pseudo-terminals and
  `/dev/urandom`, and a few named files (git's `~/.gitconfig` and
  `~/.config/git/` config, ignore and attributes, the mounts file and the
  user prelude); a secrets mount's folder is in no rule. Everything it
  starts is held the same, for good. Each program gets the run's own
  temporary folder as TMPDIR (below the inherited TMPDIR, else `/tmp`),
  which it may write and which goes when the run ends (unless the run
  started a job); `/tmp` itself and the home's caches (`~/.cache`) are
  outside it, so every program gets `XDG_CACHE_HOME` and
  `ZIG_GLOBAL_CACHE_DIR` set to `build:cache` (the `build` mount's `cache`
  folder, or the run's temporary folder's when no writable mount is named
  `build`) and `PLAYWRIGHT_BROWSERS_PATH` to `build:cache/ms-playwright`,
  each of the four unless the script's `env` sets it. **Playwright's
  browsers are installed there once**, from a shell outside Shale:
  `PLAYWRIGHT_BROWSERS_PATH=<the build mount's folder>/cache/ms-playwright
  npx playwright install chromium`, or an old `~/.cache/ms-playwright`
  moved there whole; a screenshot tool finds its Chromium by
  `PLAYWRIGHT_BROWSERS_PATH`, as roc-apps' do. ssh
  reads `~/.ssh/known_hosts` and `~/.ssh/config`, and no key file, so a
  confined ssh does not authenticate. **No program gets the session's
  sockets:** `SSH_AUTH_SOCK`, `DBUS_SESSION_BUS_ADDRESS`, `XDG_RUNTIME_DIR`
  and Claude Code's `CLAUDE_CODE_MESSAGING_*` are left out of what every
  program inherits, and an `env` (or `secrets`) that sets one is refused,
  want or not: each names a socket to a service outside the confinement
  (systemd's user manager starts any program it is asked to), and
  Landlock's file rules do not hold a socket. **`run`'s `unconfined = "tag: why"`** runs six shapes past
  the rules, each recorded as `(unconfined: ...)`
  (`c/host/host_unconfined.c`), each the system's own program, named bare
  in the argv and run as `/usr/bin/git`, `/usr/bin/ssh`, `/usr/bin/scp` or
  `/usr/bin/rsync` with `PATH=/usr/bin:/bin` (never one the inherited PATH
  finds, whose `~/.local/bin` a script may write), and with ssh's agent
  when the session has one: a `git push`, a `git fetch` and a `git
  ls-remote --symref <remote> HEAD`, and a deploy's `ssh`, `scp` and `rsync`
  to a host of the hosts file; no other git command (a `clone`, another
  `ls-remote`, a `pull`). **The push** is `git [-C dir]
  [-c color.ui=never] push [options] <remote> <refspec>...`, its options
  HOST_UNCONFINED_PUSH_FLAGS' alone (`-q`, `-u`, `--delete`,
  `--force-with-lease`, ...; no `--receive-pack` or `--exec`), a remote
  by its name and at least one refspec. It never forces (`--force`, `-f`,
  a bundle with an `f`, a `+refspec`, `--mirror`, `--prune` and `--all`
  are refused) but by `--force-with-lease`, and that never to main or
  master; it never deletes main or master. Every push URL of the remote,
  as git reads it (`git remote get-url --push --all`, run confined, so an
  `insteadOf` and a `pushurl` count), is `<line><repo>.git` for a line of
  the **remotes file**: `~/.config/shale/remotes`, beside the hosts file
  (or the file `SHALE_REMOTES` names), one owner a line,
  `git@github.com:showell/`; Shale ships none, so no file is no remote.
  It runs with its hooks, fsmonitor, credential helpers, submodule pushes
  and signing off, over ssh alone (`GIT_ALLOW_PROTOCOL=ssh`), ssh's
  `HostName` that line's host. **The fetch** is `git [-C dir] [-c
  color.ui=never] fetch [options] <remote> [<refspec>...]`, its options
  HOST_UNCONFINED_FETCH_FLAGS' alone (`-q`, `--prune`, `--tags`, ...; no
  `--upload-pack`, `--all`, `--multiple` or `--recurse-submodules`), a
  remote by its name (letters, digits, `.`, `_`, `-`) and refspecs that
  name each ref whole and force none (no `+`, `*` or `^`). Every fetch URL
  of the remote (`git remote get-url --all`, run confined: an `insteadOf`
  counts, a `pushurl` does not) is a remotes file's, as a push's. It runs
  with its hooks, fsmonitor, credential helpers, submodule fetches,
  automatic gc and maintenance, bundle URIs and commit graph off, an
  alternate's refs command `true`, the host asked for git's own
  `git-upload-pack` whatever the remote's `uploadpack` says, and the same
  ssh. **The ls-remote** is that one form alone, which asks which branch
  the remote's HEAD names, held as a fetch is (its remote by name, every
  fetch URL the remotes file's, the fetch's settings and upload-pack). A
  `-c` but `color.ui=never` is refused before any of them. The git
  prelude's `git_push`, `git_fetch` and `git_remote_head` run each over ssh
  so (below). **The deploy's** `ssh`, `scp` and `rsync` go to a host of the
  **hosts file** alone: `~/.config/shale/hosts`, beside the mounts file (or
  the file `SHALE_HOSTS` names), one `user@host` a line, `#` lines and blank
  ones skipped (`deploy@example.com`). Shale ships no host: no file, or no
  line, is no host allowed; a line that is not `user@host` stops the run
  naming it. A host is written as in the file (`user@host:path` in a
  copy): ssh with no option before its host but `-o BatchMode=yes` and `-o
  LogLevel=QUIET`, scp with `-q`, `-p`, `-O` (the original protocol, so the host runs `scp -t <path>`, which a forced command can hold to its paths) and those two `-o`s alone (no `-r`, which follows every link in a folder: a folder goes by rsync, whose options here follow none),
  rsync with its listed options and `-e ssh` (those two `-o`s after it)
  alone of a shell, no daemon module, and each local path under the root or
  a mount (a written one under a writable mount). None takes an `env` (git
  but GIT_TERMINAL_PROMPT). **The host may be a secret**, `secret_args`'
  `<secret PROD>`, as the prod deploys hand it: the shapes are checked on
  the argv the program is handed, the value in place, against the hosts
  file, and a refusal names the marker, never the value, which the scrubber
  takes out of the output as for any run; so the hosts file holds the prod
  host's `user@address` as the secret does. **gh's configuration is closed
  too:** every program gets `GH_CONFIG_DIR`, an empty folder of the run's
  own, which no script's `env` sets, and gh's token goes in by `secrets =
  {GH_TOKEN = "keys:github-token"}` alone (an `env` that sets GH_TOKEN,
  GITHUB_TOKEN or an enterprise one is refused), as
  `cookbook/build_on_github.shale` does; `gh auth` (`status` prints the
  token's first characters, `token` and `git-credential` all of it) and
  `gh alias` are refused, want or not. **One secret is fenced to one
  program:** `github-pr-token`, a classic token with `public_repo` that
  writes every public repository its owner may reach, goes to `gh pr
  create`, `gh pr view`, `gh pr close`, `gh issue create` and `gh issue
  view` alone (`c/host/host_run_secrets.c`'s HOST_SECRET_FENCED). `run`
  refuses any other use of it before it reads the secret: another program
  (`curl`, and so `http`'s `bearer`), another gh command (`gh pr merge`,
  `gh issue edit`, `close` or `comment`, `gh api -X POST`), and any option
  those five commands' lists don't name (`--web`, `--editor`). Each value
  is held to its shape: `--repo OWNER/NAME` with no host, `--head
  OWNER:BRANCH` whose owner is a github.com owner of the remotes file
  (below), and a pull request or an issue by its number. A pull request's
  create names `--repo`, `--head`, `--title` and `--body-file`, so gh never
  asks, forks or pushes; an issue's names `--repo`, `--title` and
  `--body-file`. **An issue's `--repo` is one we file on**: a repository of
  a github.com owner of the remotes file, or one of
  HOST_FENCE_ISSUE_UPSTREAMS, the one list of the upstreams (`roc-lang/roc`
  first), which `issue_upstreams()` gives a script. The token goes in
  `secrets = {GH_TOKEN = "keys:github-pr-token"}` alone, the run's only
  secret, with no `env`, `want`, `unconfined` or `stdout_secret`, and never
  in `secret_args`. It runs as the system's own gh, `/usr/bin/gh`, with
  `PATH=/usr/bin:/bin`, never one planted on PATH, and stays confined.
  `cookbook/open_pull_request.shale` and `cookbook/open_issue.shale` are its
  users. `remote_owners()` gives the remotes file's lines as `{host,
  owner}`, so a script can check a head before it runs gh. A `want` never
  widens it. A program it
  refuses gets EACCES from the call it made, so a script sees the
  program's own words, `Permission denied`, and its exit code: a refusal
  is a gap to name in host_confine.c on purpose. A kernel without Landlock
  (or older than ABI 3) runs no program, and nor does a secrets mount
  whose folder lies inside the root or another mount: no rule could hide
  it.
  A program runs in a process group of its own, and when it exits, what it
  left running there gets TERM, 2 seconds, then KILL before `run` returns:
  a server meant to outlive the call is a job (`job_start`, below).
  `must(r)` gives `r` if it exited 0; otherwise it prints its output and
  fails, naming the program and how it ended. `real(p)` gives a path's OS
  path, for an argv.
- **JSON:** `json_read(s, opts?)` gives Shale's values (an object a map,
  an array a list). Where they are not JSON's it says so: a number with a
  fraction or an exponent is its text, a string; an integer past 64 bits is
  refused; a member that is `null` is left out of its object and a `null` in
  an array refused unless `{null = v}` stands a value in; a key twice is
  refused. A malformed text stops naming what is wrong at its byte, line and
  column; `depth` (64) and `size` (the call-bytes cap) bound it.
  `json_write(v)` is compact JSON, an empty table `[]`.
- **Text:** `re_lines(s, pattern, opts?)` gives `{line, text}`, with
  `invert` and `nocase`. The host searches the lines, a step a line and
  each line's search, so a pattern over megabytes of notes fits the
  default budget when it begins with a byte that is rare in them. `head_lines(p, n)` and `tail_lines(p, n)` take a
  file of any size and give nil for a missing one. `grep_all(p, lit)` is
  `grep` for a file of any size. `grep_tree(place, needle, opts?)` is
  `grep -rn`: `{hits, files, passed, cut}`, each hit `{path, line, text}`,
  with `regex`, `nocase`, `ext = {"zig"}`, `context = N` (each hit's
  `before` and `after`) and `most`; a link, a binary file and a name the
  store can't hold (`is_name`) are in `passed`, never left out quietly.
  **Many needles in one walk:** `grep_tree(dir, {"a", "b"}, opts)` looks
  for each (literals, or patterns with `regex`), each file read once
  whatever their number, and each hit names the one it holds, `{path,
  line, text, needle}`; a line holding two is a hit for each, in the
  list's order, and `most` counts them all. A needle that is a string
  gives hits without `needle`, as before. `""`, an empty list, a map and
  an item that isn't a string stop the run.
  **`skip = {"tests/golden", "c/out"}`** leaves those folders out whole,
  unlisted and unread: each is a folder's path below the place searched,
  of names (no absolute path, mount, empty part, `.` or `..`); one that
  is not there has nothing to skip and passes, as `walk_all`'s does, and
  one that is not a folder stops the run, as does a `skip` for a lone
  file. An option `grep_tree` does not take stops the run,
  naming those it does. The walk and the search are the host's
  (`c/host/host_grep_tree.c`), in one pass: a read is a folder listed or
  a file read (a binary file costs its head alone), and each `skip` a
  stat; a run is charged a step an entry, each listing's sort, a step per
  16 bytes read and each hit's text. A file is held only while it is
  searched, so the arena keeps the hits, not the tree; a cut by `most`
  ends the walk there. A secrets mount's folder below the place is left
  out unnamed, as `walk` leaves it. `unique(list)` gives each item once. `newest(dir, n)`
  gives the names that changed last (the root as `""` or `"."`).
  `path_under(dir, name)` spells a name below a folder as every call takes
  it: `name` in the root, `m:name` in a mount's folder, else `dir/name`.
  `fresh_dir(parent, prefix)` makes a folder of the run's own below
  `parent` (`mktemp -d`, `<prefix>.XXXXXX`), so two runs at once never
  share a scratch folder; the caller removes it.
- **Jobs**, programs that outlive the call:
  - `job_start(argv, {log, cwd, env, want, keep_on_stop})` starts one under a detached
    reaper (`want` as `run`'s, for a program `run` would refuse).
    `log` is required, and there are no `secrets` or `secret_args`. A log whose job still
    runs, or whose reaper has yet to write its end, is refused: stop or
    wait for it, or use another. Once `job_wait` says `exited`, the log
    takes a new job at once. When the job's program exits, what it left
    running in its process group gets TERM, 2 seconds, then KILL before
    the reaper writes its end, as `run` does; a process that left the
    group (`setsid`) is not touched.
  - Its record is `<log>.job`. `job_alive`, `job_stop` (TERM, 2 seconds,
    then KILL) and `job_wait(job, {timeout_ms, file, contains})` take the
    job or that path, so a later call carries on.
  - `job_wait` gives `{ended = "exited" | "found" | "timeout" | "lost"[,
    code | signal]}`. `exited` always has a `code` or a `signal`; `lost` is
    a job gone with its reaper and no end written (the reaper was itself
    ended). `contains` waits for a literal in `file`, or in the job's own
    log when no `file` is given (a `file` with no `contains` is refused);
    within one call it reads only what the file gained since the last
    look, so a long wait on a large log costs the log once, and a later
    call reads it from the start. A file that cannot be read stops the
    run, named.
  - **A run that stops ends the jobs it started.** A `fail`, an error or
    a budget, anything but the script's own end, stops each job this run
    started that still runs, as `job_stop` does, and the report says so in
    one line naming each: `` ── stopped with the run, 1 job it started:
    `zig build serve` (log runs:serve.log) ``. A run that ends leaves its
    jobs running, so a server started on purpose stays up; so does a
    failure `attempt` catches, since the run goes on (a job started inside
    it ends with a later stop, as any other of the run's). `keep_on_stop =
    true` keeps a job whatever happens, and a stop names it apart, `──
    left running (keep_on_stop): ...`. A job the run already stopped or
    waited to its end is left alone, and a job another run started is
    never touched, even one this run looked at with `job_alive` or
    `job_wait`. The jobs end before
    `--undo-on-stop` puts files back, so none writes past the undo. What
    kills `shale` itself (a signal, the Bash call's timeout) never reaches
    the stop, and its jobs run on.
  - **Keep `timeout_ms` under the Bash call's own timeout** (120 s unless
    raised), because Shale cannot see it.
- **Processes that are not your jobs:** `procs()` lists each as `{pid,
  ppid, uid, name, args, start}` (`args_cut = true` past 1 MiB of command
  line), **never this run or a process above it**, so a filter of `args`
  cannot match the shell that ran you, which `pkill -f` and `ps | grep` +
  `kill` do whenever the command line holds the pattern. Match the one you
  mean exactly (`p.args == ...`, `p.name == ...`): a substring also names
  a decoy whose argv mentions it. `proc_stop(p)` takes one entry (TERM, 2
  seconds, then KILL) through a pidfd, refuses this run and those above
  it, leaves alone a pid the kernel has since given to another process
  (its `start` differs: false), and stops the run when a signal is refused
  (EPERM) or the process outlives KILL: never a success with it there.
- **Files:** `create(p, text)` refuses a file already there.
  `edits_all({{p, edits}, ...})` checks every file's edits, then writes
  them as one: a stop of any kind, a budget's included, comes before the
  first file is written, so the files change all together or not at all.
  `edit_between(p, from, to, new)` replaces what lies between two markers,
  both kept, or from `from` to the end of the file when `to` is nil: no
  offsets to work out, and a marker missing or found twice is a named
  failure with nothing written. `zig_rename(p, from, to, {lines}?)` renames a Zig identifier as
  Zig's tokenizer sees it, giving `{renamed, left_in_comments}` (the
  cookbook's "A symbol renamed across a tree"). **`{container = "C"}`**
  renames C's member instead (its declaration, its bare uses inside C, and
  `C.from`, `self.from`, `Self.from`, `@This().from` there), leaving another
  container's same name alone and stopping, with the line, at a `.from` it
  cannot attribute; **`{via = {"search_index", "harness.UidSite"}}`**
  renames `prefix.from` callers alone, and gives `left`, the lines of every
  other `.from`. `cookbook/zig/rename_member.shale` is the two across a tree:
  the declaring file, then every file that imports it, through its import's
  name.
  `zig_decls(p)` lists a Zig file's top-level declarations, from Zig's
  own parser, in file order: `{kind, name, name_form, visibility, linkage,
  first, last, start, stop, uses[, comment_above]}`. `kind` is one of
  `const`, `var`, `fn`, `test`, `comptime` or `field`; `name_form` is
  `identifier`, `string` (a test's) or `none`. A span runs from its
  doc comment to the `}`, `;` or field's `,` that ends it (`read(p, start,
  stop - start)` is it); `comment_above` is `{first, last}`, the line
  numbers of the plain `//` lines directly over it; `uses` is the top-level names its span uses (not its own, not a
  field's or a comment's), sorted: the file's reference graph, for finding
  where it divides. `zig_decls(p, {container = "Table"})` lists that
  container's own members the same way, `uses` counting its members'
  names used bare (never its own; a member used through `.` is in
  `reaches`, not `uses`), and adds `reaches`: the members a member
  touches through a `.` (`self.slots`, `self.grow()`), **by name, not by
  type** (a `.slots` of another value is counted). A file that does not
  parse stops, naming the line. **The
  analysis is Zig's parser and its tokens, not types**: what it cannot
  see (a method reached through a value), `zig build` judges after, with
  `cookbook/zig/judge_move.shale` (and `check_same_tests.shale` for the test
  names).
  `zig_move(from, to, names, {comment, after}?)` moves top-level
  declarations (a name, or `{kind, name}` when one name is two) from one
  Zig file to another (made if missing; written first, then `from` cut),
  and says what the move broke: `{moved, needs_pub, needs_import,
  needs_from_from, uses_left}`, each `{name, line}` (`needs_import` with
  its `target`, the value `from` gives it). `uses_left` is the code's uses
  only: mentions in comments are yours. Not reported: a method called
  through a value (`hold.begin()`), a name through an `@This()` alias.
  `{from_as = "gopher"}` reaches what the moved code uses of `from` through
  it, in the same call: those uses qualified (`gopher.served`), the import
  put in `to`, and `pub` put on those names in `from`; it gives `qualified`
  and `made_pub` too, and refuses a `from_as` the moved code already names.
  The moved lines, and the blank lines it puts between them, end as
  `to`'s lines do (CRLF or LF; an empty `to` as `from`'s), so neither
  file mixes the two. Declarations on lines next to each other stay so. A
  section's banner (a `// ──` line, heading what is under it to the next
  banner) goes with its section when every declaration under it moves.
  `zig_renames(p, {a = "x.a", b = "x.b"}, opts?)` is `zig_rename` of many
  names in one pass: one read, one write, one diff; each name held to
  `zig_rename`'s rules, the first refusal said, nothing written.
  `zig_tokens(p, {tag}?)` lists a Zig file's tokens, `{tag, text, line}`
  (std.zig.Token.Tag's names), by the tokenizer the other three read with:
  a plain comment is no token, a string one. `{tag = "identifier"}` keeps
  the names alone.
  `shale_tokens(p, {tag}?)` lists a Shale file's tokens, comments among them,
  by the lexer that runs the language: `{tag, text, line, start, stop}`.
  `tag` is `name`, `int`, `string`, `comment`, a keyword's `kw_end` or a
  symbol's `plus` (`lex.Tag`'s names); `text` is the source's bytes, a string's
  quotes and escapes as written; `read(p, start, stop - start)` is the token,
  as in `zig_decls`, and only space stands between tokens.
  `{tag = "comment"}` keeps the comments alone. A file that doesn't lex stops
  the run with the lexer's line and message.
  `zig_imports(p)` lists a Zig file's `@import`s and `@embedFile`s, from
  Zig's parser (a split call is one, a comment's or a string's none):
  `{kind, target, name, line, file}`, `file` being `"found"` or
  `"missing"` beside it, or `"module"` (no extension: `std`, `host_io`).
  It was zig-webbox's library_test.zig's reading too (its `zig_refs`).
  `zig_rename` and `zig_move` read identifiers one way
  (`zig-ast/zig_ast.zig`'s `identifiers`): a field, an enum's member or a nested
  container's own declaration of the name is another thing, and
  `[]const X` is a use of `X`. **Where the Zig tools read Zig:** zig-ast
  ([a repo of its own](https://github.com/showell/zig-ast)), Zig's own tokenizer and parser in a program
  of its own, which the host starts at a run's first Zig call and asks for
  each one after (one program a run, whatever the files), over
  `zig-ast/wire.zig`'s frames. The host checks the arguments, reads
  the files and writes the edits zig-ast plans through `edit`'s own
  machinery, so each change is diffed and logged as an `edit`'s. It is
  `tools:zig-ast/bin/zig-ast`, installed by `shale install.shale` in
  zig-ast's checkout (its README.md),
  or the program `SHALE_ZIG_AST` names, as Shale's own tests do; without
  either a Zig call stops, saying how to install it.
  `roc_tree(paths)` is Roc's own parser's tree of each file, in order:
  `{path, nodes, err}`, from [roc-ast](https://github.com/showell/roc-ast),
  a repo of its own whose FORMAT.md is the format's home (installed by that
  repo's install.shale under the `tools` mount as `roc-ast/bin/roc-ast`), its `--flat`
  lines read in the host, every string whole however it is quoted. A node is
  `{kind, depth, parent, kids, line, column, end_line, end_column}` and the
  attributes Roc's tree gives it, by their names (`raw`, `name`, `method`,
  `op`, `type`, `field`, ...; `module-of` as `module_of`; a positional value,
  `(unary "!"` or a tuple access's `.0`, as `value`): absent where the tree
  has none, so `get(n, "raw", "")`. `kind` is Roc's name for the node
  (`e-apply`, `e-method-call`, `args`); `parent` and `kids`
  are indices into `nodes`, `parent` 0 for the root; a node with no span
  (`args`, `statements`) has `line` 0. A file roc-ast refuses (a parse
  problem) is that file's `err`, its `nodes` empty, and the files after it
  are still read; a missing file's `err` is `no such file`; `err` is `""`
  for a file read. A step is charged a node; one program runs for the
  paths, and one more for each refusal.
  `go_tree(paths)` is the same for Go: Go's own parser (the standard
  library's go/parser) through go-ast (Shale's `tools/go_ast`, installed by
  `shale tools/go_ast/install.shale` as `go-ast/bin/go-ast` under the
  `tools` mount), which writes roc-ast's format, so one host reader takes
  both. A node's `kind` is go/ast's type name (`FuncDecl`, `CallExpr`,
  `Ident`), its attributes its scalars (`name`, `value`, `lit`, `op`,
  `tok`, `text`), and `role` is the field of its parent that holds it
  (`Fun`, `Args`, `Body`, `Results`), so a recipe never guesses which kid is
  which; `tools/go_ast/main.go`'s header has the format. A file that does
  not parse, or cannot be read, is that file's `err`. The Go recipes are
  [`cookbook/go/`](../cookbook/go/README.md).
  **The types beside the tree:**
  [roc-types](https://github.com/showell/roc-types), a repo of its own, is
  Roc's own checker at the same pin, through the compiler's embedding API:
  given an app, it writes every expression's and pattern's type, by
  region, for every module the app reaches, and fails on any error. It
  links most of the compiler, so it is built on GitHub and installed by
  that repo's install.shale (`shale install.shale` in its checkout) as
  `roc-types/bin/roc-types` under the `tools` mount. Its format's home is
  that repo's FORMAT.md; the cookbook's Roc recipes read it with
  `cookbook/roc/lib/RocTypes.roc`, and a Shale script finds the roc-types
  for its Roc with `cookbook:roc/types.shale`'s `roc_types_exe(roc)`. The
  pin roc-types was built for must be the tree's Roc's.

**Git, in the git prelude** (`c/preludes/git.shale`). Every call
runs git under `run`. A `repo` is a Shale path, `""`
or `"."` for the root and `repos:roc-chat` for a mount.
- **Reading:** `git_status(repo)` gives `{code, path[, from]}`.
  `git_log(repo, {n, range, ref, fields, paths})` gives `{hash, subject,
  date, author}` (with `paths`, the commits that touch them), `date` in ISO 8601 with a numeric offset (`+00:00`, never `Z`,
  whatever the git). `git_diff(repo, {range, paths, stat})` and `git_show(repo,
  "rev:path")` give text. `git_ls_files(repo)`, `git_is_clean(repo)` and
  `git_head(repo)` give the tracked paths, whether the tree is clean, and
  the head's short hash. `git_upstream(repo)` gives `{name, ahead,
  behind}` as the last fetch saw it, or nil with no upstream.
- **Changing:**
  - `git_commit(repo, msg, {add = "tracked" | "all" | {paths}})` passes
    the message on stdin and gives the new head; `amend = true` rewrites
    HEAD, refused once HEAD is on a remote branch.
  - `git_tag(repo, name, {rev, msg})`: annotated with `msg`, light
    without; `git_push(repo, {tag})` sends that tag alone.
  - `git_restore(repo, {paths}, {from})` puts paths back as HEAD (or
    `from`) has them, index and work tree; `git_mv(repo, from, to)`.
  - `git_push(repo)` pushes the branch HEAD is on to the branch of the
    same name on its upstream's remote (or on `{remote}`'s). **It refuses
    a branch with no upstream, or one whose upstream has another name**
    (a branch made from origin/master by hand tracks master), naming the
    call that pushes it: a bare push never lands on master unasked.
    `git_push(repo, {branch, remote})` pushes to that branch of `remote`
    (`origin` unless named) and makes it the upstream, as `push -u` does;
    `{to = "main"}` pushes HEAD (or `{from}`, a local ref) to that branch,
    the upstream left as it is; `{delete = "lane"}` deletes that branch
    on the remote; `{lease = true}`, beside a bare push, `branch` or `to`,
    forces it with `--force-with-lease`, as a lane pushes its own branch
    after a rebase. **A push over ssh runs unconfined** (ssh reads the
    user's key, which no confined program may), so `run` holds it to the
    remotes file's remotes, and refuses any force but a lease to a branch
    other than main and master, and a deletion of either (`unconfined`,
    above); a push to a local path runs confined.
  - `git_fetch(repo, {remote, all, prune})` fetches the remote a bare
    `git fetch` reads (the upstream's, else `origin`), `remote`, or every
    remote in turn; **a fetch over ssh runs unconfined**, held to the
    remotes file's remotes as a push is (`unconfined`, above), and one from
    a local path runs confined. `git_pull_ff(repo)` is `git_fetch` and a
    fast-forward to the upstream (never `git pull`'s own fetch), and
    `git_merge_ff(repo, ref)` a fast-forward to `ref`.
  - `git_remote_head(repo, remote)` asks the remote which branch its HEAD
    names (`ls-remote --symref`, unconfined over ssh) and sets
    `<remote>/HEAD` here to it, as `git remote set-head --auto` would (which
    runs confined, and so cannot reach an ssh remote); nil for none.
  - `git_merge(repo, ref)` makes a merge commit when one is needed and
    gives the new head. On a conflict it undoes the merge (`merge
    --abort`) and stops, naming the conflicted paths, so the checkout is
    never left half merged.
  - `git_worktree_add(repo, dir, {branch, new})` (or `{rev}`, detached;
    or `{branch, new = true, rev = "origin/master"}`, a new branch from a
    rev, as a lane starts). A `new` branch tracks nothing, so its first
    push is `git_push(repo, {branch})`, to a branch of its own name;
    `git_worktree_remove(repo, dir, {force})` and `git_branch_delete(repo,
    name, {force})`. Git removes a worktree holding an initialized
    submodule only by `force`, which also throws away its changes: a lane
    is removed by `cookbook/remove_lane.shale`, which forces it only once
    the lane is merged and each submodule is clean with every commit on a
    remote.
- **Errors:** a git error stops the run naming the git command and git's
  first line: `` `git show HEAD:nope` exited 128: fatal: ... ``; a call
  that changes things prints git's output first (its first and last 10
  lines). Each call has 60 seconds; those that take options
  (`git_log`, `git_diff`, `git_commit`, `git_push`, `git_fetch`, `git_tag`,
  `git_restore`, `git_mv`,
  `git_merge`, `git_worktree_add`, `git_worktree_remove`,
  `git_branch_delete`) take `timeout_ms` to change it.

**Claude's commit belongs in the user prelude**, not the git prelude: it
refuses a message without its attribution.

```lua
-- ~/.config/shale/prelude.shale
function commit(repo, msg, opts?)
  local signed = false
  for l in lines(msg) do
    if starts_with(l, "Co-Authored-By:") then signed = true end
  end
  if not signed then fail("`commit`: the message has no Co-Authored-By: line") end
  return git_commit(repo, msg, opts)
end
```

**A repo's own prelude** is `<repo>/.shale/prelude.shale` at the root. **A
user's** is `~/.config/shale/prelude.shale` (`SHALE_PRELUDE=<file>` names
another, `SHALE_PRELUDE=` none). Each holds only functions, and none may
take an earlier prelude's name. A stop in one names it:
`stopped at .shale/prelude.shale:5:12`. A function's last parameters may
be optional (`function f(a, opts?)`): a call that leaves them out gives
them nil.

**A library is a file of functions and `const`s that a script names**,
`use("cookbook:roc/pinned.shale")`, a top-level statement: its names are
the script's from that line on. **Reach for one when recipes copy a
helper**: a recipe runs from other repositories' roots, so a repo's prelude
cannot serve it, and a library is found through a mount wherever it runs.
A repo's prelude stays the tool for what every script at that root wants.
The path is a mount's, or beside the script's own file (`./times.shale`,
`./<folder>/x.shale`), never the root's or an absolute one: a recipe and its
library load together wherever they live, lanes included, so a lane's
`shale build:lanes/x/c/check_discipline.shale` loads the lane's
`c/build/c_items.shale` (its `use("./build/c_items.shale")`), not the
box's. A `./` path is names only (no `..`), and a
script on stdin or `-e` has no file to be beside, so it names its library
through a mount. Either way the file is a read of the run's (no link, no
secrets folder). It holds functions and literal `const`s only, each from
the start of its line, and uses no other library; a name it shares with a
builtin, a prelude, another library or the script's own function or local
is refused at the `use`, naming both places. A stop inside it names its
file and line. SHALE.md, "Libraries: `use`", has the rules.

Every block of Shale below ran as a test of Zig Shale's
(`legacy_zig/src/script/agent_test.zig`, frozen), on a small checkout
shaped like zig-metal-unikernel's, a fresh one each, and printed what the
box after it says, exactly. Each block is a golden case too
(`tests/golden/docs-SHALE-AGENTS-*`), run without that checkout.

## The cookbook, beside what an agent would write otherwise

The short recipes are here, each tested as written. **The longer ones are
files** in `cookbook/`, and
[`cookbook/README.md`](../cookbook/README.md) lists every one in a line:
what it does, and a launch to start it with, or the `use` that loads a
library. Among them are `look.shale` (lines by number, and a literal's lines
across folders, mounts or a git revision), `find_phrase.shale` (a phrase in
wrapped prose, found and edited), `run_zig_build.shale` (a build's verdict
without its noise), `check_disk_space.shale` (a run stopped under a floor of
free disk, each verify's first check) and `times.shale` (a library: a
stage's time against its recent runs). Copy one and change it; not
everything is a builtin.

**A recipe's name says what it does.** One that runs starts with a verb
(`find_negatives.shale`), one of `cookbook/recipe_verbs.txt`; a library,
which `use` loads, is named for what it holds (`merge_conflicts.shale`).
`cookbook/check_recipe_names.shale` holds the cookbook to it, telling a
library by its code (a top level of `const` and `function` alone);
`report=1` lists each break with a suggested name.

**Where a script lives:** a script starts in the repository that uses it,
in that repo's own scripts folder. It moves to the cookbook lazily, once it
has proven itself and a second repository wants it; reuse is never forced,
and a near-copy that fits its repo is fine. A repository that isn't
Shale's keeps its custom scripts, with a short README in their folder
naming them and the cookbook recipes it relies on.

**A long-lived recipe may be written in Roc**, on Shale's Roc platform,
typed and confined as a Shale script is: [`roc_platform/README.md`](../roc_platform/README.md).
One-offs stay in Shale.

**A recipe over a whole tree** keeps each file's work in a function and
calls `yield()` after each file, as `cookbook/check_seals.shale` and the name
hunts do: what a file's call made (its tokens, its lines) is let go there,
so the run's memory is one file's, not the tree's. Without it a tree of a
few hundred files runs out of arena.

Each task says what it uses: a literal finds the lines (`grep`), and a
pattern takes the field out of them (`re_find`), where a field has a
shape. A literal that is the whole of what is wanted stays a literal.

### The DEPARTS lines in `src/tcp*.zig`, counted by reason

```shale
local by = {}
local total = 0
for p in glob("src/tcp*.zig") do
  for m in grep(p, "DEPARTS (") do
    local r = re_find(m.text, "DEPARTS \\((\\w+)\\)")
    if r ~= nil then
      by[r.group] = get(by, r.group, 0) + 1
      total = total + 1
    end
  end
end
local rows = {}
for k in keys(by) do push(rows, {reason = k, lines = by[k]}) end
sort(rows, "reason")
print(table(rows, {"reason", "lines"}))
print("all", total)
```

```output
reason   lines
bounded      1
fast         1
simpler      1
all	3
```

```sh
grep -h 'DEPARTS (' src/tcp*.zig | sed -E 's/.*DEPARTS \(([a-z]+)\).*/\1/' | sort | uniq -c
```

**Uses** `glob`, `grep` for the lines, `re_find` for the reason (the
`sed` expression's own pattern, and its group). The shell is one line, and
wins here. Shale's version is longer, but each step is a name an agent can
change.

### The files over a thousand lines

```shale
local rows = {}
for p in glob("**/*.zig") do
  local n = count_lines(p)
  if n > arg("over", 1000) then push(rows, {lines = n, path = p}) end
end
sort(rows, "lines", "desc")
print(table(rows, {"lines", "path"}))
```

```output
lines  path
 1200  src/big_test.zig
```

```sh
find . -name '*.zig' -not -path './.*' | xargs wc -l | awk '$1 > 1000 && $2 != "total"' | sort -rn
```

**Uses** `glob` and `count_lines`: no text is taken apart. About even.
`count_lines` counts a last line without its newline, as `wc -l` does
not.

### Every `props.always` message, with where it is

```shale
for p in glob("src/**/*.zig") do
  for m in grep(p, "props.always(") do
    local r = re_find(m.text, [=[props\.always\([^"]*"((?:\\.|[^"\\])*)"]=])
    if r ~= nil then print(p .. ":" .. m.line, r.group) end
  end
end
```

```output
src/tcp.zig:7	tcp: what is sent was queued
src/tcp_timeouts.zig:3	tcp: a deadline is after the start
```

```python
import pathlib, re
for p in sorted(pathlib.Path("src").rglob("*.zig")):
    for i, line in enumerate(p.read_text().splitlines(), 1):
        if "props.always(" in line and (m := re.search(r'"([^"]*)"', line)):
            print(f"{p}:{i}\t{m.group(1)}")
```

**Uses** `grep` for the lines and `re_find` for the message: the call's
first string literal, not merely the line's first string.
`(?:\\.|[^"\\])*` takes the message whole, an escaped quote (`\"`) and
all, as Python's does: the group that does not capture repeats, the one
that does keeps the message.

### A symbol renamed across a tree, as Zig sees it

```shale-linux
local n = 0
for p in glob("src/**/*.zig") do
  if #grep(p, "pastDeadline") > 0 then
    local r = zig_rename(p, "pastDeadline", "deadlinePassed")
    n = n + r.renamed
    if r.left_in_comments > 0 then print(p .. ": " .. r.left_in_comments .. " left in comments") end
    -- What reaches it through a module is after a `.`: its own edit.
    if #grep(p, "timeouts.pastDeadline") > 0 then edit(p, "timeouts.pastDeadline", "timeouts.deadlinePassed", {all = true}) end
  end
end
print(n .. " renamed")
-- (the CLI's summary of each diff; --verbose for the diffs)
```

```output
edit src/tcp.zig: 1 hunk, -1 +1 lines
edit src/tcp.zig: 1 hunk, -1 +1 lines
edit src/tcp_timeouts.zig: 1 hunk, -1 +1 lines
2 renamed
```

```sh
grep -rlw pastDeadline src | xargs sed -i 's/\bpastDeadline\b/deadlinePassed/g' && git diff
```

**Uses** `zig_rename`: Zig's own tokenizer, so it renames identifier
tokens and nothing else: not a field or an enum literal after `.`
(`.watch`), not `@"pastDeadline"`, not a
string, a test's name or a comment (it counts the comments' mentions in
`left_in_comments`, for you to decide), and not part of a longer name.
`sed`'s `\b`, and `edit`'s `word`, take every one of those. **The
analysis is tokens, not types:** a name reached through a module
(`timeouts.pastDeadline`) is after a `.` and is left, so the qualified
uses get their own `edit`, and `zig build` judges after, with
`cookbook/zig/judge_move.shale` (and `check_same_tests.shale` for the test
names). `opts.lines = {first, last}` limits it to a range.

**Moving a block to another file, then its uses:** move the declarations, then rename what the old file still says to the
qualified name:

```shale-linux
local r = zig_rename("src/linux_host.zig", "kept_write_deadline_ms", "linux_writes.kept_write_deadline_ms")
-- stops: argument
```

A `to` with a `.` where the name is still **declared** (after `const`,
`var` or `fn`, before a `:`, in a capture, an enum's member) stops the run,
naming the line, with nothing written (`pub const
linux_writes.kept_write_deadline_ms` is not Zig): the stop says the block
was not moved yet, as it does here. The rename
writes as it goes, so `git diff` shows it and `git checkout` takes it
back. (A `shale-linux` block ran with the Linux prelude, in
`legacy_zig/src/shale_doc_linux_test.zig`.)

### Two edits to one file, each asserted, one write

```shale
local n = edit("src/tcp.zig", {
  {old = [=[
fn check() void {
    props.always(@src(), true, "tcp: what is sent was queued", .{});
}]=], new = [=[
fn check(queued: bool) void {
    props.always(@src(), queued, "tcp: what is sent was queued", .{});
}]=]},
  {old = "// DEPARTS (simpler): no SACK.", new = "// DEPARTS (simpler): no SACK, no window scaling."},
})
print(n .. " edits")
```

```output
--- a/src/tcp.zig
+++ b/src/tcp.zig
@@ -4,1 +4,1 @@
-// DEPARTS (simpler): no SACK.
+// DEPARTS (simpler): no SACK, no window scaling.
@@ -6,3 +6,3 @@
-fn check() void {
-    props.always(@src(), true, "tcp: what is sent was queued", .{});
-}
+fn check(queued: bool) void {
+    props.always(@src(), queued, "tcp: what is sent was queued", .{});
+}
2 edits
```

```python
import pathlib
p = pathlib.Path("src/tcp.zig"); s = p.read_text()
for old, new in [('fn check() void {\n    props.always(@src(), true, "tcp: what is sent was queued", .{});\n}',
                  'fn check(queued: bool) void {\n    props.always(@src(), queued, "tcp: what is sent was queued", .{});\n}'),
                 ("// DEPARTS (simpler): no SACK.", "// DEPARTS (simpler): no SACK, no window scaling.")]:
    assert s.count(old) == 1, old
    s = s.replace(old, new)
p.write_text(s)
```

**Uses** `edit`'s list form and long strings: each `old` is held to being
there exactly once, in the file as the edit before left it; one diff is
printed, and the file is written once, or not at all if any edit fails.
**About even**, and Shale's says what it changes before anything is
written (a one-line summary; the diff with `--verbose`).
The text goes in as it is, quotes and
lines and all, with no escaping.

### A doc's `file:line` references, checked

```shale
for m in grep("docs/notes.md", ".zig:") do
  for w in words(m.text) do
    local r = re_find(w, "(\\w+\\.zig):(\\d+)")
    if r ~= nil then
      local name = r.groups[1]
      local n = parse_int(r.groups[2])
      local found = glob("**/" .. name)
      if #found == 0 then
        print(name .. ":" .. n, "no such file")
      elseif count_lines(found[1]) < n then
        print(name .. ":" .. n, found[1] .. " has " .. count_lines(found[1]) .. " lines")
      end
    end
  end
end
```

```output
tcp_timeouts.zig:40	src/tcp_timeouts.zig has 5 lines
gone.zig:1	no such file
```

```python
import pathlib, re
for m in re.finditer(r"([\w/]+\.zig):(\d+)", pathlib.Path("docs/notes.md").read_text()):
    hits = list(pathlib.Path(".").rglob(pathlib.Path(m[1]).name))
    if not hits: print(m[0], "no such file")
    elif len(hits[0].read_text().splitlines()) < int(m[2]): print(m[0], "too short")
```

**Uses** `grep` for the lines, `words` for each reference, `re_find` for
the name and the number (its two groups, `groups[1]` and `groups[2]`), and
`glob` and `count_lines` to check it.

## If you write Lua

Shale reads like Lua, and an agent's Lua habits are most of its first
stops. Most are a clear stop;
the first two can give a quiet wrong answer, an empty list:

- **Patterns are regular expressions, not Lua's.** The escape is `\`
  (`"\\."` in a string), and `%` is a plain character, so Lua's `%d+` or
  `[%w_]` matches nothing it should and finds nothing, quietly. Up to nine
  capture groups, and `(?:...)` for one that does not capture. A match is
  `{at, text}`, and with groups also `groups`, a list of each one's text in
  order of its `(`, and `group`, the first's. Lua's `gsub` is
  `re_replace(s, pattern, replacement)`: every match replaced, `\1` to `\9`
  (`"\\1"` in a string) a group's text, not `%1`.
- **`grep(p, text)` is plain text**, and finding nothing is an empty list,
  not an error. So `grep(p, "edit%(")` quietly finds nothing. A pattern over
  a file is `re_lines(read(p), pattern)`.
- **A missing key is a stop, not nil**: `has_key(m, k)` first, or
  `get(m, k, default)`. `(m[k] or 0)` stops on the index.
- **`take`, `last` and `drop` take lists**; a part of a string is
  `sub(s, i, j)`.
- **There is no `write`**: `replace(p, s)` writes a file whole, `append`
  adds, and `create` refuses one already there.
- **Functions are not values**: no closures and no anonymous functions.
  Declare `function name(...)` at the top level (no `local function`). An
  argument a caller may leave out is declared `opts?`.
- **`now()` is when the run started**, the same at every call, so `now() -
  t0` is always 0: a program's time is `run`'s own `ms` on its answer.
  To wait, `sleep(ms)` (0 to 600000 a call, Linux): it is no program, so
  it spends none of a run's 64.
- **A launch value given to a program where it reads options**
  (`step=`, `rev=`) goes through `not_option(who, name, v)`, which refuses
  one that starts with `-`: the program would take it as an option.
- **A function sees only its parameters, its locals and the `const`s
  above it**, never the script's `local`s: `const X = 1`, or pass it in.
- **One name per `local`**: `local a, b = 1, 2` is refused, and so are
  multiple assignment and multiple returns (return a list or a map).
- **Integers only**: no floats, so `/` is refused; `//` divides (it
  floors, `-7 // 2` is -4), and `tonumber` reads an integer.
- **Loops are `for x in l do`** over a list's items (no `ipairs`, no
  `pairs`, no `range`), or `while`. Not-equal is `~=`.
- **`run`'s keep is a size**: `whole = <bytes>`, `tail = <lines>` or
  `head = <lines>`, and `timeout_ms` is required. `must(r)` takes `run`'s
  answer; the git prelude's calls stop by themselves (`git_commit` gives
  the new head, `git_push` nothing), so no `must` around them.
- **A path is the root's.** Another place is a mount, `name:path`; the
  box's are in `~/.config/shale/mounts`. An absolute path is refused, and
  the stop says how to write it: for `/tmp/claude-1000/<session>/x.out` it
  names the mount `scratch` and the path `scratch:<session>/x.out` (the
  deepest mount that holds it, or the root's own path). One that no mount
  holds, or that is in a `secrets` mount's folder, gets the plain stop,
  which points at `shale --help`'s list of mounts.
  A mount line ending in ` ro` is read-only: every change through it
  (`edit`, `replace`, `create`, `append`, `remove`, `mkdir`,
  `remove_tree`, `edits_all`, `edit_between`, `zig_rename`, `zig_move`,
  and `run`'s `log` and `job_start`'s log and record) stops, `refused:
  ... mount `x` is read-only`, before anything is written; reads work.
  Those logs are changes to `--undo-on-stop` too: a stopped run puts back
  what a log's truncate took, and removes a log it made. A
  writable mount inside a read-only one decides for its own folder
  (`memory:` inside `projects:`), and through the read-only one's name
  that folder stays read-only. A `run` with its `cwd` there runs: Shale
  cannot see what a program writes.
- **A newer compiler is a `tools` mount.** `run` refuses a program by a
  path git does not track (a script just written?), and a toolchain's
  install is one. A mount line with ` tools` after its path
  (`zig017 = ~/zig-0.17.0 ro tools`) lets `run` take any program under it
  by path, `real("zig017:zig")`; a shell or text tool there is refused all
  the same. **Adding that line is the user's call:** when a new compiler is
  installed, ask once, then add the line; never pass `want` on every run
  instead.
- **Tokens are a `secrets` mount** (`keys = ~/.shale-keys secrets`):
  read-only, and no script reads, lists, `stat`s or `real`s its files, by
  its name or by a root or mount path that reaches its folder; no program
  starts there or is handed its folder in an argument (with or without
  `want`). Only `run`'s `secrets = {NAME = "keys:file"}` (and
  `secret_args`, below) takes one, the value in the program's environment and scrubbed from its output as
  `<secret NAME>`; `http_get`'s `bearer` and `cookbook:fetch_page.shale`'s
  `bearer=keys:file` use it, and a `bearer` goes only to a URL that begins
  `https://api.github.com/` or `https://chat.lynrummy.com/` (the core
  prelude's `bearer_url_held`), any other a stop. **The scrubber catches
  accidents, not intent:** it takes a token out of what a program prints,
  and nothing stops a script that means to send one elsewhere (by `run`'s
  `secrets` to a program of its choosing); the mounts and the secrets
  folder make the box's discipline plain, and a hostile agent is a threat
  they do not answer. **`run`'s `secret_args = {NAME = "keys:file"}`
  puts one in an argument**, for a program that takes it there (ssh's
  destination, rsync's `HOST:/path`): each `<secret NAME>` in the argv,
  a whole argument or inside one, is the value in the argv the program
  is handed, and the script's own argv, with its markers, is what the
  answer's `argv`, the record, a stop and the `── time:` line show, as
  the scrubber shows the value in the output. It is held before any file
  is read: a name that is not a word (letters, digits, `_`), one also in
  `secrets`, a name no argument uses, a `<secret ` no name of it gives and
  a marker in argv[0] (the guards read the program's name as the script
  wrote it) each stop the run; the file is read as `secrets` reads one,
  and `job_start` refuses it as it refuses `secrets`. **The scrubber takes
  out the value whole and any 12 bytes of it or more**, so a program
  that prints a token's first characters (`gh auth status`) or a fragment
  shows none of it; a part shorter than 12 bytes passes (ssh's `connect to
  host <address>` for a `user@address` with a short address): quiet the
  program (`-o LogLevel=QUIET`). An argument is not an
  environment: any process on the machine can read a program's argv in
  `/proc`, so an argument suits an address, not a token. **The one write
  path is `run`'s `stdout_secret = "keys:NAME"`:** the program's stdout goes straight to
  that file, never to the script, so a secret received is stored without
  passing through a variable. The file is directly in a `secrets` mount
  (no folder in it, no `..`, no link), made 0600 beside it and renamed
  into place only on exit 0: a failed program leaves nothing, and one that
  prints nothing, or past 64 KiB, stops the run with nothing stored. The
  kernel holds the file to one byte past 64 KiB while the program runs
  (`RLIMIT_FSIZE`, for every file it writes), so a runaway never fills the
  disk. A
  file already there is refused before the program runs, unless
  `secret_replace = true`. `stdout_secret` takes no keep, filter or log
  (`whole`, `head`, `tail`, `re`, `not_re`, `nocase`, `before`, `after`,
  `width`, `log`): stdout has one destination, and the result's `out` is
  stderr's last 20 lines, scrubbed as ever, beside `secret_written` and
  `secret_bytes`. A link to the file from elsewhere (an old
  `~/.token`) is not the mount's and is not guarded. **What this guard
  is:** it stops Shale's own reads, and a program handed an obvious path
  to a secret, or to a folder that holds one (an argument, an `env` value
  or a name holding a NUL byte is refused first, since the kernel would cut
  it there); **a program is not
  sandboxed.** One that builds a path itself, takes it after a prefix
  (`-Tkeys/tok`, `@keys/tok`) or follows a link it made, can reach a
  secret. Keep a secrets mount out of the folders a script hands to
  programs.
- **The store follows no symbolic link.** A path any part of which, below
  the root or a mount's own folder, is a link stops the run, `refused:
  <path> is a symbolic link`, for a read, a change, `exists`, `stat` and
  a listing alike. A mount's own path is resolved once, when the mounts
  are read.

## Setting up your own secrets folder

A token an agent receives (an API key redeemed over HTTP) belongs in a
`secrets` mount, and reaches a program only through `run`.

- **The folder:** `mkdir -m 700 ~/.shale-keys`. In a cloud session where
  git runs as root and builds run as another user, the folder belongs to
  the build user, the one `shale` runs as.
- **The mount:** a line in the mounts file (`~/.config/shale/mounts`, or
  the file `SHALE_MOUNTS` names), `name = path` then its words:
  `keys = ~/.shale-keys secrets`.

**`shale --secrets`, run in your own terminal,** stores a secret you have
and sets up Claude Code's side, so `~/.claude/settings.json` needs no
editing by hand. It shows a menu of three commands:

1. **where** prints each secrets mount's name and folder, and nothing else.
2. **store** asks for the mount (when there are two or more) and the
   secret's name, which must be a plain file directly in the mount, checked
   as `stdout_secret`'s file is. It reads the value with the terminal's echo
   and line editing off, a byte at a time to the newline (a backspace takes
   one back), so a long token is read whole, and refuses one past 64 KiB.
   The terminal's settings come back however it ends, a ^C included. It
   writes the value as `stdout_secret` does: mode 0600, beside the file
   first, then renamed into place. A secret already there is replaced only
   after a yes, and an empty value stores nothing. It prints `stored
   keys:NAME (N bytes)`, never the value, and then offers the deny rules
   when any is missing.
3. **deny** adds a `Read` and an `Edit` rule for each secrets folder to
   `permissions.deny` in `~/.claude/settings.json`, so Claude Code's own
   file tools (Read, Edit, Write, Grep and Glob) stay out of it as Shale
   does. A rule is `Read(//<home>/.shale-keys/**)`: the folder's absolute
   path after `//`, as Claude Code's permission rules write one. It shows the rules it will
   add and asks first. It adds only the rules that are missing, inside the
   one `permissions` block, and writes nothing to a file that holds them all
   already. It keeps the old file as `settings.json.bak` and writes the new
   one beside it and renames it into place, laid out two spaces a level, its
   keys in their order. A file that does not parse is left as it is.

It refuses to start unless its stdin and its stdout are both a terminal,
and the gate refuses `shale --secrets` too, so an agent cannot run it: an
agent asks the person to.

**Storing a secret received** is `run`'s `stdout_secret`, the program's
stdout written to the file and never to the script. Where the service
answers with the secret alone:

```lua
local r = run({"curl", "-s", "https://example.org/redeem", "-d", "code=..."},
  {timeout_ms = 30000, stdout_secret = "keys:example_key"})
if not r.secret_written then fail("not stored: curl exited " .. r.code .. ", " .. r.out) end
```

A service that wraps the secret in JSON or other text needs a program
that prints the secret alone; the script never takes it apart.

**Using it** is `run`'s `secrets`, the value in the program's environment
and scrubbed from its output. curl reads it there with `--variable`, as
`cookbook/read_zulip_topic.shale` does:

```lua
run({"curl", "-s", "--variable", "%EXAMPLE_KEY",
  "--expand-header", "Authorization: Bearer {{EXAMPLE_KEY:trim}}",
  "https://example.org/api/me"},
  {timeout_ms = 30000, tail = 20, secrets = {EXAMPLE_KEY = "keys:example_key"}})
```

**In an argument** is `run`'s `secret_args`, where the program reads it
from its argv, as ssh, rsync and scp read a destination:

```lua
local dest = {PROD = "keys:prod-ssh"}
run({"ssh", "-o", "BatchMode=yes", "-o", "LogLevel=QUIET", "<secret PROD>", "uptime"},
  {timeout_ms = 30000, tail = 5, secret_args = dest})
run({"rsync", "-rlt", "site/", "<secret PROD>:/srv/site/"},
  {timeout_ms = 300000, tail = 20, secret_args = dest})
```

**Never print a secret, and never write one anywhere else**: not to
scratch, a log or a file to move later. `stdout_secret` is the one way in.

## The gate: `shale-gate` (in `~/.claude/settings.json`)

**A PreToolUse hook on Bash and Monitor** holds a session to Shale
(`c/shale_gate.c`, installed by `shale c/install.shale` as
`~/tools/shale/bin/shale-gate` and run by `tools/shale-gate.sh`, which turns
any failure, a missing binary included, into a refusal). It is a port of
the Zig gate (`legacy_zig/tools/shale_gate.zig`, frozen), and the C build's tiers hold it to the answer frozen
for every case of its golden cases (`c/build/gate_golden.shale`). It passes
exactly:

- **a launch:** `[cd DIR && ]shale` (DIR `~/showell_repos`, a folder below
  it, or a mount's folder), or an absolute path ending `/shale`,
  then any of `--verbose`, `--undo-on-stop`, `--args-stdin`, `--budget
  NAME=N`, and plain words (a FILE, `mount:path` included, and
  `NAME=VALUE`s), every word plain (no quotes, `$`, backticks, `\`, a tab,
  or `;|&<>()*?`; `-e` and `--root` are refused), optionally ending
  `<<'TAG'` (TAG `[A-Z_]+`), with the script below and a last line `TAG`
  that no earlier line is;
- **the escape:** a first line `# want: <tag>: <text>` (Shale should do
  this) or `# out-of-scope: <tag>: <text>` (it never will), the tag
  `[a-z0-9-]+` and the text not empty. The command runs, and a line goes
  to `~/.local/state/shale/wants.tsv`: the date (UTC), the session, the
  kind, the tag, the text, and the command's first 160 bytes, tabs and
  newlines escaped. An escape that cannot be logged (no `HOME`, a write
  that fails) is refused, not run. `shale cookbook:read_wants.shale` reads it
  back (today's, or `since=`, with counts by kind, tag and session),
  through a mount over that folder (`state = ~/.local/state/shale ro`).

A subagent is gated like the session that started it: its hook input
carries an `agent_id`, and the gate reads it like any other call.

Anything else is refused, with one line naming both forms. `shale
c/install.shale` puts `shale` on `PATH` as a link to
`~/tools/shale/bin/shale`, so an install is what every launch runs. The README's "Using Shale with Claude Code on your project" has the
hook's settings, how to turn it off, a block for `<project>/CLAUDE.md`, and
what the gate refuses with what to do instead.

### A root-owned install (prepared, not yet the box's)

The hook's wrapper, the gate and the runtime above all live where a
session can write them: `tools/shale-gate.sh` in the checkout (the
`repos` mount), `~/tools/shale/bin` (the `tools` mount) and the
`~/.local/bin/shale` link (the `localbin` mount). A root install takes
them out of reach. The owner of the box runs, from a clean checkout:

    shale c/install.shale dry=1     # build and check c/out/release, writing nothing
    sudo tools/install_root.sh      # install it root's, at /usr/local/lib/shale

`tools/install_root.sh` (its header has the rest) puts the runtime and
the gate in `/usr/local/lib/shale/bin`, `tools/shale-gate-root.sh` as
`/usr/local/lib/shale/shale-gate.sh` (a wrapper that runs that gate alone:
no `SHALE_GATE` or `HOME` picks it), and `/usr/local/bin/shale`. Then, by
hand, once:

- the hook's command in `~/.claude/settings.json` becomes
  `/usr/local/lib/shale/shale-gate.sh`;
- `rm ~/.local/bin/shale`, so PATH finds `/usr/local/bin/shale`;
- in `~/.config/shale/mounts`, the two lines become read-only:

      tools = ~/tools ro tools
      localbin = ~/.local/bin ro

**What changes in the install flow:** `shale c/install.shale` writes
`tools:shale/bin` and `localbin:`, which a read-only mount refuses, so a
Shale install is the two commands above; the other installers that write
`tools:` (zig-ast's, roc-ast's, roc-types', `tools/go_ast/install.shale`)
stop on the read-only mount too, so the owner adds a writable line for one
(`toolsrw = ~/tools`, its `dest=` pointed there) and takes it out after,
or installs those root's the same way. A program a `tools` mount holds
still runs by path, as now.

**Claude Code's own tools** pass by the gate (README, "Using Shale with
Claude Code"), so the owner's `~/.claude/settings.json` holds them back
with deny rules. `Edit(...)` covers Write, MultiEdit and NotebookEdit too
(a `Write(path)` rule never matches); `~/` is the home and `//` the file
system's root (a bare `/` in user settings is `~/.claude/`). A Read deny
holds Read, Grep and Glob, and a Bash command only where Claude Code can
see the file named, so the gate and the confinement remain the shell's
guard:

```json
{"permissions": {"deny": [
  "Edit(~/.claude/**)",
  "Edit(~/showell_repos/.claude/**)",
  "Edit(~/.local/bin/**)",
  "Edit(~/tools/**)",
  "Edit(~/showell_repos/shale/tools/shale-gate.sh)",
  "Read(~/.ssh/**)"
]}}
```

`Edit(~/.claude/**)` also holds back the memory folder
(`~/.claude/projects/<project>/memory/`), which a session writes with
Edit and Write; where it should, a narrower list keeps it:
`Edit(~/.claude/settings.json)`, `Edit(~/.claude/settings.local.json)`,
`Edit(~/.claude/CLAUDE.md)`, `Edit(~/.claude/agents/**)`,
`Edit(~/.claude/skills/**)`. A deny rule wins over any allow.

## Where Shale is worse, said plainly

- **Regular expressions are a subset**: up to nine capture groups (and
  `(?:...)`), counts (`{n,m}`) up to 100, no lookaround, no backreferences
  (each refused by name). In return no pattern makes a search slower than linear, and its
  cost is bounded before it runs, and the match is the one a backtracker
  would find, loops whose turn can take nothing among them.
- **A file is read whole by `grep` and `edit`**, at most the call-bytes cap
  (64 MiB under `shale`, `--budget call_bytes=N`; 1 MiB on a site): a
  bigger one goes through `read_lines` a part at a time.
- **Step budgets are real.** A walk of a large tree, or a script that loops
  over every line in Shale rather than in a builtin, runs out of the
  5,000,000 steps a run has unset; `--budget steps=N`, or the script's own
  `-- budget: steps=N` line, raises them, and the stop says which one ran
  out. The two memory budgets beside the arena are raised the same way,
  and their stops say how: `kept_bytes` (what a `yield` keeps, 1 MiB unset,
  at most 256 MiB) and `machine_bytes` (the code, the frames and the
  operands, 16 MiB unset, at most 1 GiB).
- **Paths are a store's**: every part must be a name FAT holds, so a file
  whose name has `:` or `"` cannot be named, and hidden folders are left
  out of a `glob` unless `{hidden = true}`.
- **A program is run whole, not piped:** `run`'s `stdin` is a string or a
  `whole` result, so a pipeline is two runs with the first's output in
  memory, not a stream; `run`'s own filters (`re`, `not_re`, `before`,
  `after`) keep the lines a `| grep` would. Every run needs `timeout_ms`
  and a keep.
- **No network but a program's**: `http(method, url, {body, headers,
  bearer, timeout_ms})` (the Linux prelude's, by `curl`) gives `{status,
  headers, body, ms}`, `http_get(url, ...)` its GET, and
  `cookbook:fetch_page.shale` is a GET from a launch line; and **no
  environment**: a script cannot read a variable, only give one to a
  program (`env`, `secrets`).
- **Git is a prelude over `git`**, not a library: each call is a program
  run, about as fast as typing it, and its errors are git's first line.
- **Jobs are polled:** `job_wait` looks after 20 ms, then twice as late
  each time, to once a second; nothing tells a script the moment a job
  ends.
- **Integers only, bytes only**: no floats, and a string is its bytes.
