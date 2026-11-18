# shale

Shale is a small scripting language for working on code. One line greps a
whole repo. A short script refactors one. Every run is budgeted, confined,
readable. Agents write it in place of bash, in place of Python.

Shale scripts look like Lua. A C runtime reads each one, compiles it to
bytecode and runs it on a small virtual machine. Its builtins cover the
common Linux work: reading, editing and searching files, walking folders,
running programs and background jobs, git, text and JSON. The whole runtime
builds as one C file, links only the C library, and weighs under a
megabyte. It currently runs on Linux x86-64, as the only shell on our
development box: every command our agents issue goes through it, and a hook
refuses anything else.

## Why use it instead of bash or Python

Every script runs inside guard rails that a shell does not have:

- **`run` never hangs or floods.** A program runs only with a timeout and a
  keep (`head`, `tail` or `whole`), so a hung test is stopped and a
  megabyte of build log comes back as its last 40 lines.
- **Every run has budgets**: steps, reads, bytes read and bytes printed
  (64 KiB unset). A run that hits one stops and says which; `--budget
  NAME=N` raises it.
- **A stop is one line** that says where and why, with a class (`type`,
  `index`, `refused`, `budget reads`), not a traceback.
- **It is confined.** A script reads and writes only under the folder it
  runs in and the mounts you name (`name:path`): no `..`, no absolute
  paths.
- **It refuses the risky shortcut, by name.** `run` refuses a program at a
  path git does not track (a script just written, not yet read), a shell or
  an interpreter (`bash`, `python3`, `node`), and a text tool Shale already
  has (`grep`, `sed`); `{want = "tag: why"}` runs one anyway, logged. A
  `name=value` the script never reads stops the run before it starts, so a
  misspelled argument is caught.
- **Changes are undoable.** Work in a git checkout and `git diff` shows
  what a run did. `--undo-on-stop` puts back every file a stopped run
  changed. `git_merge` that hits a conflict aborts the merge and stops,
  naming the paths, so a checkout is never left half merged.

## Install
**The source is C, in [c/](c/)**, the only Shale since 2026-11-17: the
runtime (`c/shale.c`, `c/script`, `c/host`, `c/foundation`), the gate
(`c/shale_gate.c`, `c/gate`) and the preludes (`c/preludes`). It needs
Linux on x86-64, GCC and TCC.

- **Build and install:** `shale c/install.shale` from a clean checkout
  builds the release binary, runs the fast tier and a smoke test, copies it
  to `~/tools/shale/bin/shale` and points `~/.local/bin/shale` at it (a
  `localbin = ~/.local/bin` mount line is needed). The hook runs the gate
  it installs beside it. [c/docs/building.md](c/docs/building.md) has the
  whole ladder.
- **Test:** `shale c/build.shale` is the fast tier, in seconds; `shale
  c/build.shale full=1` adds the GCC release build, ASan and the planted
  faults. `shale tests/golden.shale` holds a build to every golden case.

C Shale's build scripts are themselves Shale, so a first install on a
machine with no `shale` needs one to run them. Zig Shale, the first
implementation, deprecated and frozen in [legacy_zig/](legacy_zig/README.md),
can build that first one.

## Run a script

A script's root is the folder `shale` runs in. Three ways to start one:

    shale <<'EOF'                         # a heredoc: the usual way
    print("hello from " .. #glob("src/**/*") .. " files")
    EOF

    shale find.shale pattern=job_alive    # a file, with name=value arguments

    shale -e 'print(1 + 1)'               # a one-liner (refused by the gate: outside it only)

A script reads an argument as `arg("pattern", "TODO")`, the second value
its default. A value that cannot be a plain word (a space, `*`, `|`) goes
on stdin, one `name=value` a line, after `--args-stdin`.

**Mounts:** a folder outside the root is a mount, listed as `name =
/abs/path` in `~/.config/shale/mounts` and reached as `name:file`. Add `
ro` after the path for a read-only mount. With

    cookbook = /ABSOLUTE/PATH/TO/shale/cookbook

the recipes launch from any checkout, as `shale cookbook:look.shale ...`.

## A real task

Every line holding a literal under a tree, with its file and line number,
saved as a file named find.shale:

```lua
-- find.shale: every line holding a literal, with its file and line number
local pat = arg("pattern", "TODO")
local dir = arg("dir", "c")
local hits = 0
for p in glob(dir .. "/**/*.c") do
  for g in grep(p, pat) do
    print(p .. ":" .. g.line .. ": " .. g.text)
    hits = hits + 1
  end
end
print(hits .. " lines hold " .. pat)
```

Run in this checkout,

    shale find.shale pattern=job_alive dir=c/host

prints lines like `c/host/host_jobs.c:487:     if (str_eq(name,
S("job_alive"))) {`, then `7 lines hold job_alive`. (`shale
cookbook:look.shale find=job_alive in=c/host ext=c` is the same search as a
ready recipe.)

Running a program and keeping its tail:

```lua
print(must(run({"git", "log", "--oneline"}, {timeout_ms = 10000, tail = 3})).out)
```

prints `… 216 lines left out …` and the three oldest commits. `run`
returns `{code, out, ms, ...}` and does not fail on a non-zero exit;
`must(r)` does, printing the output and naming the program.

## The traps a newcomer hits first

Each is a one-line stop, so it costs a retry, not a wrong answer:

- **`run` needs `timeout_ms` and a keep**: `run({"zig", "build"},
  {timeout_ms = 600000, tail = 40})`. Without either it refuses to start.
- **A function is declared above where it is called.** `print(f(1))` before
  `function f(x)` stops: "no function `f` above this line". Functions are
  top-level `function name(...)`; there are no closures.
- **A long string is `[=[ ... ]=]`.** Lua's `[[ ... ]]` is refused, because
  code being edited is full of `]]`.
- **A mount path is not resolved inside argv.** `run({"ls",
  "cookbook:zig"}, ...)` hands `ls` the literal `cookbook:zig`; use
  `real("cookbook:zig")` for the OS path.
- **`for x in` a map is refused**; walk `keys(m)`. And a missing key is a
  stop, not nil: `has_key(m, k)` or `get(m, k, default)`.

The rest of Lua's habits, the quiet ones first (patterns are regular
expressions, not Lua's `%d`), are in
[docs/SHALE-AGENTS.md](docs/SHALE-AGENTS.md), "If you write Lua".

## Where to go next

- **[docs/SHALE-AGENTS.md](docs/SHALE-AGENTS.md)**, for an agent: its
  "Before you reach for the shell" table gives the Shale form of each shell
  habit (`grep -rn`, `sed -n`, `git`, `curl`, a build's errors), then the
  Linux prelude (`run`, jobs, git, http) and the traps.
- **[cookbook/](cookbook/README.md)**, recipes to run or copy, one line
  each in its README: `look.shale` (find,
  or show line ranges), `run.shale` (a program's verdict and tail),
  `run_zig_build.shale`, `rename_words.shale`, `check_syntax.shale`, `fetch_page.shale`.
  Hunts over a Zig tree are in [cookbook/zig/](cookbook/zig/README.md)
  (`find_seams`, `find_smells`, `find_dead_code`, `find_clones`, ...), Roc's in
  [cookbook/roc/](cookbook/roc/README.md), and Go's in
  [cookbook/go/](cookbook/go/README.md).
- **[The tutorial](https://lynrummy.com/shale/tutorial.html)**: fifteen
  lessons that run in the browser, the language from the start
  (`legacy_zig/web/lessons.js`: the browser build is Zig Shale's).
- **[docs/SHALE.md](docs/SHALE.md)**, the reference: the language, every
  builtin with its cost and errors, the budgets, the runner's flags.

---

The rest of this README is for running Shale as Claude Code's shell on your
own project, and for working on Shale itself.

## Where it runs

- **On the box:** the `shale` CLI, C Shale since 2026-11-17, is every
  top-level shell call an agent makes (`tools/shale-gate.sh`, the hook; [docs/SHALE-AGENTS.md](docs/SHALE-AGENTS.md)),
  with Zig's own parser behind `zig_decls`, `zig_move` and `zig_rename`:
  zig-ast (`tools/zig_ast`), a program of its own that the host asks, so
  the host imports no `std.zig` parser. It is a Zig package of its own,
  with its own build and tests ([tools/zig_ast/README.md](tools/zig_ast/README.md));
  `shale tools/zig_ast/install.shale` installs it where the host looks,
  `~/tools/zig-ast/bin/zig-ast` under the `tools` mount.
- **On a live site, until 2026-11-18:** Zig Shale ran signed scripts inside
  [angry-gopher](https://github.com/showell/angry-gopher) (lynrummy.com),
  on Linux and on
  [zig-metal-unikernel](https://github.com/showell/zig-metal-unikernel),
  through its `/admin/script` route. Shale's signer, `shale-sign`, was
  removed on 2026-11-17, and the route on 2026-11-18, so no site runs Shale
  now. [dog-park](https://github.com/showell/dog-park) ran it too, until
  that repo was retired on 2026-11-16.

## Using Shale with Claude Code on your project

Build it and set up the cookbook mount as above. The gate below covers only
the shell: **Claude's own Read, Edit, Write and Grep tools are untouched.**
Mount lines also take ` tools`, for a toolchain whose programs a script may
run by path (a newer compiler, a project's `.venv`), a decision you make once,
by hand; and ` secrets`, for a folder of tokens: no script reads its files,
and only `run`'s `secrets` (and `http_get`'s `bearer`) hands one to a
program.

### Hook it into Claude Code

A PreToolUse hook sends every Bash call Claude makes through `shale-gate`,
which lets through a `shale` launch or a logged escape and refuses anything
else. In `~/.claude/settings.json` (every project) or
`<project>/.claude/settings.json` (one project), merged into any `"hooks"`
already there:

```json
{"hooks": {"PreToolUse": [{"matcher": "Bash|Monitor",
  "hooks": [{"type": "command", "command": "/ABSOLUTE/PATH/TO/shale/tools/shale-gate.sh"}]}]}}
```

- The path must be absolute, and `shale c/install.shale` must have run
  first: **the gate fails closed**, so without
  `~/tools/shale/bin/shale-gate` (or the binary `SHALE_GATE` names) every
  Bash call is refused.
- **To turn it off,** remove that block (Claude Code's `/hooks` menu shows
  it).
- A subagent Claude starts is gated like the session that started it.

### Tell Claude how

Claude learns a gate from its refusals, which is slow. Put this in
`<project>/CLAUDE.md` (or `~/.claude/CLAUDE.md` for every project):

```markdown
## Shale
Top-level Bash calls go through Shale's gate: run a `shale` launch
(`shale <<'EOF'` ... `EOF`, or `shale FILE name=value ...`), plain words only.
Read /ABSOLUTE/PATH/TO/shale/docs/SHALE-AGENTS.md before the first one: its
"Before you reach for the shell" table and "If you write Lua" traps.
When Shale truly can't do it, escape with a first line
`# want: <tag>: <what Shale lacked>`; the call runs and is logged.
```

### What the gate refuses, and what to do instead

| you'd type | what happens | instead |
|---|---|---|
| `npm test`, `git status`, `pytest` at the top level | refused: only a `shale` launch passes | from a script: `print(must(run({"npm", "test"}, {timeout_ms = 600000, tail = 40})).out)` (below); or the escape `# want: tests: run npm test` as the call's first line |
| `node <script>.js`, `python3 -m ...`, `bash -c ...` | refused inside `run` too: Shale is the script | `{want = "tag: why"}` in `run`'s options runs it, recorded in the run's effects (`--verbose`); or the escape |
| `grep`, `sed`, `cat`, `head` | refused: Shale has builtins for these | `grep(p, text)`, `read`, `lines`, `look.shale` (below) |
| `<project>/node_modules/.bin/tsc`, `<project>/.venv/bin/pytest` | refused: a program by a path git does not track | `npx tsc`; for a venv, a ` tools` mount (`venv = <project>/.venv ro tools`, the path absolute) and `run({real("venv:bin/pytest")}, ...)` |
| `npm`, `npx`, `git`, `pytest`, `cargo`, a compiler by name | allowed inside `run` | `run` needs `timeout_ms` and a keep: `head = N`, `tail = N` or `whole = BYTES` |

**`run` returns, it does not print or fail:** its result holds `out` (what
the program printed, as much as the keep keeps) and `code`. A failing test
run does not stop the script unless you ask: `must(r)` gives `r` when the
program exited 0, and otherwise prints its output and stops the run, naming
the command and its exit. So `print(must(run(...)).out)` shows the tests'
last lines and fails when they fail.

Every gate escape (`# want:`) lands in `~/.local/state/shale/wants.tsv`: the
list of what Shale should learn next
([docs/SHALE-WANTS.md](docs/SHALE-WANTS.md) is ours). `cookbook/read_wants.shale`
reads it back, given a `state = ~/.local/state/shale ro` mount.

### Recipes for any project

The cookbook's general recipes work on any language's files:

    shale cookbook:look.shale find=TODO in=src,tests ext=ts,py   # every TODO, with its line
    shale cookbook:look.shale at=src/app.ts:40-80,README.md:1-20
    shale cookbook:rename_words.shale from=fetchUser to=loadUser ext=ts,tsx

(`*` is not a plain word for the gate, so `ext=` names extensions; a full
glob goes on stdin as `like=`, with `--args-stdin`.) The rename, written out
as a heredoc to change:

```lua
for e in walk_all(".", {kind = "file", like = "*.ts", skip = "node_modules"}) do
  if #grep(e.path, "fetchUser") > 0 then edit(e.path, "fetchUser", "loadUser", {all = true, word = true}) end
end
```

`word` matches whole words only (`fetchUserName` is left), but in strings
and comments too: read the summary, then `git diff`. `glob("**/*.ts")`
enters `node_modules`; `walk_all` with `skip` does not. The `zig/` and
`roc/` cookbooks are for those languages only.

## Checks

    shale c/build.shale           # the fast tier: every C test, the discipline, the surface, the gate: seconds
    shale c/build.shale full=1    # adds the GCC release build, ASan, the planted faults, the cookbook's self-tests and the docs' citations
    shale tests/golden.shale      # a build held to every golden case, from the outside

[c/docs/building.md](c/docs/building.md) says what each tier runs and
costs. The golden tests are the one judge of behaviour
([tests/harness/README.md](tests/harness/README.md)). `shale
bench/baseline.shale` is the whole program's baseline, the runtime on an
agent's workloads and the build's time, one TSV a run
([bench/README.md](bench/README.md)).

**Zig Shale**, the first implementation, is kept in
[legacy_zig/](legacy_zig/README.md), deprecated and frozen: its modules,
its `zig build` steps (the unit tests, the hunt, the coverage floor, the
browser build) and their tools are there, and get no fixes.

## Docs

- [docs/SHALE.md](docs/SHALE.md): the reference (above).
- [docs/SHALE-AGENTS.md](docs/SHALE-AGENTS.md): Shale as an agent's tool on
  Linux.
- [docs/SHALE-WANTS.md](docs/SHALE-WANTS.md): the ledger of what Claude
  reached past Shale for.
- [docs/PORTING.md](docs/PORTING.md): which modules are bound to Linux,
  and what a port to another kernel would replace.
- [docs/SURFACE.txt](docs/SURFACE.txt): a record of the surface, every
  builtin, stop text, budget, flag and report line, generated from the
  source by `tools/surface.shale` and checked against it by `zig build
  surface-check` (in `test`) and by C's fast tier (`c/build/surface.shale`),
  so a change shows in its diff. It is no promise that the C matches the
  Zig: the goldens judge the C.
- [docs/C-SEAMS.md](docs/C-SEAMS.md): the C port's shared design, its values,
  the arena's fidelity, the VM and the host's seams.
- [c/docs/building.md](c/docs/building.md): how the C is compiled, with TCC,
  GCC and its sanitizers, during the port and after it.
- [tools/zig_ast/README.md](tools/zig_ast/README.md): zig-ast, the program
  behind the Zig builtins, and the wire C Shale speaks to it.
- [legacy_zig/README.md](legacy_zig/README.md): Zig Shale, the first
  implementation, deprecated and frozen.
- [cookbook/zig/README.md](cookbook/zig/README.md): recipes for finding a
  Zig file's seams, moving code along them and judging the move, each
  tested to parse. Recipes, not builtins: copy one and change it.
- [cookbook/roc/README.md](cookbook/roc/README.md): recipes for a Roc
  tree: long lines laid out by `roc fmt`, and two archives judged the same
  machine code.
- [cookbook/go/README.md](cookbook/go/README.md): hunts over a Go tree,
  from Go's own parser: awkward names, nested calls, long and deep
  functions.
- [docs/designs/DESIGN-shale-stream.md](docs/designs/DESIGN-shale-stream.md):
  a background run's output streamed as it runs; proposed, parked.

## Status

| | |
|---|---|
| Source | C, in `c/`; Zig Shale, frozen, in `legacy_zig/`, its history before this repo zig-webbox's (`git -C ../zig-webbox log -- src/script`) |
| Runs | C Shale on the box, as every agent's shell (since 2026-11-17); Zig Shale, the first implementation, matched byte for byte by the C, ran in angry-gopher until 2026-11-18 |
| Tests | the golden tests (`shale tests/golden.shale`, 1,128 cases), the one judge of behaviour; C's own (`shale c/build.shale`); the Zig's unit tests, frozen with it in `legacy_zig/`; the request-edge harness for its number parses runs in angry-gopher |

## Lineage

Shale began on 2026-10-13 inside zig-webbox, the Zig web stack behind
lynrummy.com, as a small Lua-like language for one narrow job: letting a
signed script change a live site's data through its store, confined to
the store's root and held to budgets, on Linux and on a bare-metal
unikernel alike. A Linux runner came the same day, and the box's agents
started using it to read and change their own files. Within days it had
grown `run`, jobs, git, mounts and the Zig tools, and it moved into a repo
of its own on 2026-10-18. Soon after, it became the only shell on the box:
a hook refuses every other command, so the language grew from what agents
actually reached for (`docs/SHALE-WANTS.md` is that ledger). By November
its design had settled enough to freeze, and on 2026-11-17 it was
rewritten in C in about a day, against the Zig as its oracle, for faster
builds and a substrate that needs no LLVM. The golden tests
([tests/harness/README.md](tests/harness/README.md)), each input frozen
with its output, became the one judge of its behaviour. The bare-metal site
ran the Zig, the implementation Shale was born in, until angry-gopher
stopped running scripts on 2026-11-18; every `shale` on the box is C.
