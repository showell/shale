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
  `index`, `store`, `budget reads`. No traceback to read through.
- **A run makes its changes; git is the undo.** There is no dry run:
  every change is checked and logged, an `edit`'s
  diff is printed before it is written, and an edit that would not pin its
  text down is refused before anything is changed. Work in a checkout, so
  `git diff` shows what a run did and `git checkout --` takes it back.
- **It reads only what it is rooted at** (`--root`, else `SHALE_ROOT`, else
  the working directory) and its mounts (`runs:x`): no `..`, no absolute paths. It
  reaches past them only by running a program (`run`).

`shale` is terse: past half of any budget, or on a stop, it says what the
run used of four, steps, reads, read bytes and output bytes (`── used:
52374 of 5000000 steps, 12 of 10000 reads, ...`), so a script near an edge
is known before a bigger run finds it;
`--verbose` says so always, with the effects and the programs run. A run
that spent a second or more in programs, job waits and sleeps says where,
the slowest program named (`── time: 3 programs, 52.1 s in all; the
slowest 51.8 s: roc build ...`): when a step is slow, that line says
which program to look at. The
arena is not among the four: a stop on it (`budget arena`, 16 MiB unset)
says nothing of what it would have needed: raise it and run again
(`--budget arena_bytes=4000000000`), and if the script is one you keep,
put it in its `-- budget:` line. An
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
zig build                      # zig-out/bin/shale: put it on PATH
shale count.shale dir=src
shale rename.shale from=old to=new
shale <<'EOF'
for p in glob("src/*.zig") do print(p) end
EOF
```

(`-e SOURCE` runs a script given on the command line too, but the gate
refuses it: its quotes and `*` are not plain words. It is for a subagent,
or a shell outside the gate.)

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
| `pkill -f pat`, `ps aux \| grep pat \| xargs kill` | `for p in procs() do if find(p.args, "pat") ~= nil then proc_stop(p) end end`: never this run or its shell |
| `grep -rn 'text' src build.zig` | `shale cookbook:look.shale find=text in=src,build.zig ext=zig` (a value with `(`, `*` or a space: `--args-stdin`) |
| `grep -rn 'a phrase' docs` over prose, which misses one a line break splits | `shale cookbook:prose.shale find="a phrase" in=docs` (phrase on stdin with `--args-stdin`): each paragraph, list item, quote and comment block read whole (never a code fence), so a break, an indent or a run of spaces inside the phrase is one space; a hit says `path:line` where it starts, `(N lines)` when it crosses breaks, `(markup inside)` when only `*` or a backtick splits it; `new=` shows the edit and `write=1` makes it, rewrapping only the lines the edit moves |
| grep in Zig's std, a sibling repo | the same, through a mount: `in=zig:lib/std/Build.zig`, `repos:dog-park/src` |
| `zig build test 2>&1 \| grep error \| sort \| uniq -c` | `shale cookbook:zig_build.shale step=test` (`zig=` another compiler, through a `tools` mount): the summary, each error once with its count, failed tests by name, the whole output kept in a log |
| `zig build fuzz 2>&1 \| grep FAIL` | the same with `grep=FAIL,hunts ran` (a value with a space: on stdin, `--args-stdin`): every output line holding one of the literals, after the summary |
| `roc check f && roc test f 2>&1 \| tail`, any other program and its verdict | `shale cookbook:run.shale` with `cmd=roc check f && roc test f` on stdin (`--args-stdin`): each command in order until one fails, how it ended and its time, its last lines (`tail=`), lines holding `grep=` literals, the whole output in .zig-cache/run.log where that folder is (else `log=PATH`, or not kept, said); `want=tag: why` for a program `run` refuses (`node`, `sh`) |
| `python3 -m py_compile f`, `node --check f`, `zig ast-check f`, `bash -n f`: did an edit's syntax survive? | `shale cookbook:syntax.shale files=f,g` (or `rev=HEAD~1..HEAD`): one verdict a file by its kind (`.zig`, `.zon`, `.roc`, `.js`, `.mjs`, `.shale`, `.py`, `.sh`), then PASS or FAIL |
| `grep -rn ".field =" src`: does anything but its own file make a sealed value (a checked id, a path)? | `shale cookbook:sealed.shale dir=src seals=path_seal:src/script/builtins_store.zig` (several trees: `dir=a,b`; a header only one file sets: `headers=location:<file>`): Zig's tokens, so a comment or a string never counts; one line a forger, then PASS or FAIL |
| `curl -s URL \| jq .messages[0].id`, an API's JSON | in a heredoc, `json_read(http_get(url).body).messages[1].id` (an object a map, an array a list, from 1); `json_write(v)` back |
| `curl -s URL \| grep X`, a wait for a deploy | `shale cookbook:http.shale url=URL grep=X` (`wait=LITERAL minutes=N`: until the body holds it; `bearer=` a Shale path to a token file) |
| `curl -u email:KEY` a Zulip topic, or any API wanting basic auth from a secret | `shale cookbook:zulip.shale --args-stdin` (`site=`, `email=`, `key=keys:<file>`, `stream=`, `topic=`, `num=`): the key reaches curl alone, by its environment (`--variable`/`--expand-user`), as `run`'s `secrets` |
| `curl -sN .../stream \| grep @CC`, hearing a roc-chat post meant for you | `shale cookbook:listen.shale site=https://chat.lynrummy.com conv=3 me=agent:2 key=keys:<file> once=1` (`after=` the last id handled; without `once=1`, `log=` a file each line is appended to): one line a post for the agent, `#<id> by <principal>: <first line>`. A post is for it when the server's `mentions` name the agent (roc-chat decides who a post names, skipping quoted and fenced text) and is not its own; no rule limits agents answering agents, as the roc-chat server bounds agent-to-agent traffic (15 an hour since a human post); at 12 of the agent's own it prints one `budget:` line, once a crossing, and runs on. The stream is held in windows of `window=` seconds (20), so a post is heard at most a window late. A refused key, an ended stream, a gap or a message without `mentions` (a server older than them) stops it, named |
| `curl -X POST --data ... -c jar -b jar`, a response's headers | in a heredoc, `http("POST", url, {body, headers})`: `{status, headers, body}`, the headers by lowercased name (`r.headers["set-cookie"]`), a cookie sent back as a `"Cookie: ..."` header |
| `git tag -a v6 X -m ... && git push origin v6`, `git commit --amend`, ahead/behind | `git_tag(repo, "v6", {rev, msg})`, `git_push(repo, {tag = "v6"})`; `git_commit(repo, msg, {amend = true})`; `git_upstream(repo)` |
| `git checkout -- f`, `git mv a b` | `git_restore(repo, {"f"})`, `git_mv(repo, "a", "b")` |
| `git show origin/master:QUEUE.md \| sed -n 40,80p`, `git grep` at a revision | `shale cookbook:look.shale rev=origin/master at=QUEUE.md:40-80` (or `find=` `in=`): `at` and `in` read as the revision holds them (`repo=` another checkout) |
| `git log`, `git diff --stat`, `git show rev:path`, `git status` | the git prelude: `git_log`, `git_diff`, `git_show`, `git_status`, `git_is_clean` (below) |
| `git commit -m ... && git push` | `git_commit(repo, msg, {add})`, `git_push(repo)`; `git_worktree_add`, `git_merge_ff` |
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
`count_lines` for a file of any size, `re_find(s, pattern)` and
`re_match` (regular expressions, a linear-time subset: `|`, `\b`, up to
nine capture groups and `(?:...)`, `{nocase = true}`), `between(s, a, b)` (the text after one string and before the
next), `edit(p, old, new[, {all, word}])` (a literal replaced, its diff
printed) and `edit(p, {{old = ..., new = ...}, ...})` (several in sequence,
one diff, one write, none made if any fails), long strings (`[=[ ... ]=]`,
raw, for text that holds quotes and lines), `walk` with `skip`, and the
language's own `split`, `words`, `contains`, `sort`, `table` and maps. The
whole language is [`SHALE.md`](SHALE.md).

**On Linux, the Linux prelude** (`src/shale_prelude_linux.shale`; metal
has none of it):
- **Programs:** `run(argv, opts)` runs one and gives `{argv, code, ended,
  ms, out, ...}`. `timeout_ms` and a keep, `head` or `tail` (lines) or
  `whole` (bytes), are required (unless `stdout_secret`); the rest are
  `cwd`, `stdin`, `env`, `secrets`, `stdout_secret`, `secret_replace`,
  `log`, `want`, and the filters `re`, `not_re`, `nocase`,
  `before`, `after` and `width` (`Opts` in `src/shale_host_run.zig`). Its
  `cwd` is a path as every path is: under the root, or through a mount
  (`cwd = "repos:shale"` runs a sibling repo's step from another's
  checkout); `..` and an absolute path are refused. **What `run` refuses**
  unless its options say why (`want = "tag: why"`, logged in the run's
  effects): a shell or an interpreter (`bash`, `sh`, `python3`, `node`,
  `perl`: Shale is the script), a text tool (`grep`, `sed`, `cat`, `head`,
  ...: the builtins do it), a wrapper (`env`, `timeout`, `xargs`, ...: run
  the program itself), and a program by a path git does not track, unless
  it is under a ` tools` mount (a toolchain's install).
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
  `invert` and `nocase`. `head_lines(p, n)` and `tail_lines(p, n)` take a
  file of any size and give nil for a missing one. `grep_all(p, lit)` is
  `grep` for a file of any size. `unique(list)` gives each item once. `newest(dir, n)`
  gives the names that changed last (the root as `""` or `"."`).
  `path_under(dir, name)` spells a name below a folder as every call takes
  it: `name` in the root, `m:name` in a mount's folder, else `dir/name`.
  `fresh_dir(parent, prefix)` makes a folder of the run's own below
  `parent` (`mktemp -d`, `<prefix>.XXXXXX`), so two runs at once never
  share a scratch folder; the caller removes it.
- **Jobs**, programs that outlive the call:
  - `job_start(argv, {log, cwd, env, want})` starts one under a detached
    reaper (`want` as `run`'s, for a program `run` would refuse).
    `log` is required, and there are no `secrets`. A log whose job still
    runs, or whose reaper has yet to write its end, is refused: stop or
    wait for it, or use another. Once `job_wait` says `exited`, the log
    takes a new job at once.
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
  `cookbook/zig/move_judge.shale` (and `same_tests.shale` for the test
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
  `zig_imports(p)` lists a Zig file's `@import`s and `@embedFile`s, from
  Zig's parser (a split call is one, a comment's or a string's none):
  `{kind, target, name, line, file}`, `file` being `"found"` or
  `"missing"` beside it, or `"module"` (no extension: `std`, `host_io`).
  It is zig-webbox's library_test.zig's reading too (its `zig_refs`).
  `zig_rename` and `zig_move` read identifiers one way
  (`shale_zig_ast.identifiers`): a field, an enum's member or a nested
  container's own declaration of the name is another thing, and
  `[]const X` is a use of `X`.

**Git, in the git prelude** (`src/shale_prelude_git.shale`). Every call
runs git under `run`. A `repo` is a Shale path, `""`
or `"."` for the root and `repos:dog-park` for a mount.
- **Reading:** `git_status(repo)` gives `{code, path[, from]}`.
  `git_log(repo, {n, range, ref, fields})` gives `{hash, subject, date,
  author}`, `date` in ISO 8601 with a numeric offset (`+00:00`, never `Z`,
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
  - `git_push(repo)` pushes to the branch's upstream (an error when it has
    none); `git_push(repo, {branch, remote})` pushes to that branch of
    `remote` (`origin` unless named) and makes it the upstream, as `push
    -u` does.
  - `git_fetch(repo, {all})`, `git_pull_ff(repo)` and `git_merge_ff(repo,
    ref)`.
  - `git_merge(repo, ref)` makes a merge commit when one is needed and
    gives the new head. On a conflict it undoes the merge (`merge
    --abort`) and stops, naming the conflicted paths, so the checkout is
    never left half merged.
  - `git_worktree_add(repo, dir, {branch, new})` (or `{rev}`, detached),
    `git_worktree_remove(repo, dir, {force})` and `git_branch_delete(repo,
    name, {force})`.
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

**A repo's own prelude** is `.shale/prelude.shale` at the root. **A
user's** is `~/.config/shale/prelude.shale` (`SHALE_PRELUDE=<file>` names
another, `SHALE_PRELUDE=` none). Each holds only functions, and none may
take an earlier prelude's name. A stop in one names it:
`stopped at .shale/prelude.shale:5:12`. A function's last parameters may
be optional (`function f(a, opts?)`): a call that leaves them out gives
them nil.

Every block of Shale below runs as a test (`src/script/agent_test.zig`), on
a small checkout shaped like zig-metal-unikernel's, a fresh one each. What it
prints is in the box after it, exactly.

## The cookbook, beside what an agent would write otherwise

The short recipes are here, each tested as written. **The longer ones are
files** in `cookbook/`: `look.shale` (lines by number, and a literal's
lines across folders, mounts or a git revision), `prose.shale` (a phrase
in wrapped prose, found and edited) and `zig_build.shale` (a build's
verdict without its noise) for any project; the Zig ones in
[`cookbook/zig/`](../cookbook/zig/README.md) find a file's seams, move
declarations along them and judge the move; `roc/` and `zig17/` are those
work's. Copy one and change it; not everything is a builtin.

**A recipe over a whole tree** keeps each file's work in a function and
calls `yield()` after each file, as `cookbook/sealed.shale` and the name
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
`cookbook/zig/move_judge.shale` (and `same_tests.shale` for the test
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
back. (A `shale-linux` block runs with the Linux prelude, in
`src/shale_doc_linux_test.zig`.)

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
  `[%w_]` matches nothing it should and finds nothing, quietly. One capture
  group at most, and no `(?:`. A match is `{at, text, group}`. Lua's
  `gsub` is `re_replace(s, pattern, replacement)`: every match replaced,
  `\1` (`"\\1"` in a string) the group's text, not `%1`.
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
  box's are in `~/.config/shale/mounts`. An absolute path is refused.
  A mount line ending in ` ro` is read-only: every change through it
  (`edit`, `replace`, `create`, `append`, `remove`, `mkdir`,
  `remove_tree`, `edits_all`, `edit_between`, `zig_rename`, `zig_move`) stops, `refused:
  ... mount `x` is read-only`, before anything is written; reads work. A
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
  `want`). Only `run`'s `secrets = {NAME = "keys:file"}` takes one, the
  value in the program's environment and scrubbed from its output as
  `<secret NAME>`; `http_get`'s `bearer` and `cookbook:http.shale`'s
  `bearer=keys:file` use it. **The one write path is `run`'s
  `stdout_secret = "keys:NAME"`:** the program's stdout goes straight to
  that file, never to the script, so a secret received is stored without
  passing through a variable. The file is directly in a `secrets` mount
  (no folder in it, no `..`, no link), made 0600 beside it and renamed
  into place only on exit 0: a failed program leaves nothing, and one that
  prints nothing, or past 64 KiB, stops the run with nothing stored. A
  file already there is refused before the program runs, unless
  `secret_replace = true`. `stdout_secret` takes no keep, filter or log
  (`whole`, `head`, `tail`, `re`, `not_re`, `nocase`, `before`, `after`,
  `width`, `log`): stdout has one destination, and the result's `out` is
  stderr's last 20 lines, scrubbed as ever, beside `secret_written` and
  `secret_bytes`. A link to the file from elsewhere (an old
  `~/.token`) is not the mount's and is not guarded. **What this guard
  is:** it stops Shale's own reads, and a program handed an obvious path
  to a secret, or to a folder that holds one; **a program is not
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
`cookbook/zulip.shale` does:

```lua
run({"curl", "-s", "--variable", "%EXAMPLE_KEY",
  "--expand-header", "Authorization: Bearer {{EXAMPLE_KEY:trim}}",
  "https://example.org/api/me"},
  {timeout_ms = 30000, tail = 20, secrets = {EXAMPLE_KEY = "keys:example_key"}})
```

**Never print a secret, and never write one anywhere else**: not to
scratch, a log or a file to move later. `stdout_secret` is the one way in.

## The gate: `shale-gate` (in `~/.claude/settings.json`)

**A PreToolUse hook on Bash and Monitor** holds a session to Shale
(`tools/shale_gate.zig`, run by `tools/shale-gate.sh`, which turns any
failure, a missing binary included, into a refusal). It passes exactly:

- **a launch:** `[cd DIR && ]shale`, or an absolute path ending `/shale`,
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
  that fails) is refused, not run. `shale cookbook:wants.shale` reads it
  back (today's, or `since=`, with counts by kind, tag and session),
  through a mount over that folder (`state = ~/.local/state/shale ro`);
- **a subagent's call:** its hook input carries an `agent_id` (the
  top-level session's carries none), so subagents are not gated.

Anything else is refused, with one line naming both forms. Put `shale` on
`PATH` as a link to `zig-out/bin/shale`, so a rebuild is what every launch
runs. The README's "Using Shale with Claude Code on your project" has the
hook's settings, how to turn it off, a block for `<project>/CLAUDE.md`, and
what the gate refuses with what to do instead.

## Where Shale is worse, said plainly

- **Regular expressions are a subset**: up to nine capture groups (and
  `(?:...)`), no `{n,m}`, no lookaround, no backreferences (each refused by
  name). In return no pattern makes a search slower than linear, and its
  cost is known before it runs, and the match is the one a backtracker
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
  `cookbook:http.shale` is a GET from a launch line; and **no
  environment**: a script cannot read a variable, only give one to a
  program (`env`, `secrets`).
- **Git is a prelude over `git`**, not a library: each call is a program
  run, about as fast as typing it, and its errors are git's first line.
- **Jobs are polled:** `job_wait` looks after 20 ms, then twice as late
  each time, to once a second; nothing tells a script the moment a job
  ends.
- **Integers only, bytes only**: no floats, and a string is its bytes.
