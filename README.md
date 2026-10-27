# shale

**Shale** is a small scripting language that looks like Lua, for reading,
searching and changing a project's files and for running its programs. On
the box it is built for, agents use it in place of bash, Python, `sed`,
`awk` and `grep`: a script is written as a heredoc, run, and its output
read.

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

Linux on x86-64 and Zig 0.17.0 (`zig version`). Shale builds against
[zig-webbox](https://github.com/showell/zig-webbox), checked out **beside**
it:

    git clone https://github.com/showell/shale
    git clone https://github.com/showell/zig-webbox
    cd shale && zig build -Doptimize=ReleaseSafe   # zig-out/bin: shale, shale-gate, shale-sign

Put `zig-out/bin` on your PATH, or link `zig-out/bin/shale` into a folder
that is.

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
local dir = arg("dir", "src")
local hits = 0
for p in glob(dir .. "/**/*.zig") do
  for g in grep(p, pat) do
    print(p .. ":" .. g.line .. ": " .. g.text)
    hits = hits + 1
  end
end
print(hits .. " lines hold " .. pat)
```

Run in this checkout,

    shale find.shale pattern=job_alive dir=src

prints thirteen lines like `src/shale_host_jobs.zig:21: if (std.mem.eql(u8, name,
"job_alive")) {`, then `13 lines hold job_alive`. (`shale
cookbook:look.shale find=job_alive in=src ext=zig` is the same search as a
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
- **[cookbook/](cookbook)**, recipes to run or copy: `look.shale` (find,
  or show line ranges), `run.shale` (a program's verdict and tail),
  `zig_build.shale`, `rename_words.shale`, `syntax.shale`, `http.shale`.
  Hunts over a Zig tree are in [cookbook/zig/](cookbook/zig/README.md)
  (`seams`, `smells`, `dead`, `clones`, ...), and Roc's in
  [cookbook/roc/](cookbook/roc/README.md).
- **[The tutorial](https://lynrummy.com/shale/tutorial.html)**: fifteen
  lessons that run in the browser, the language from the start
  (`web/lessons.js`).
- **[docs/SHALE.md](docs/SHALE.md)**, the reference: the language, every
  builtin with its cost and errors, the budgets, the runner's flags.

---

The rest of this README is for running Shale as Claude Code's shell on your
own project, and for working on Shale itself.

## Where it runs

- **On the box:** the `shale` CLI is every top-level shell call an agent
  makes (`tools/shale-gate.sh`, the hook; [docs/SHALE-AGENTS.md](docs/SHALE-AGENTS.md)),
  with Zig's own parser behind `zig_decls`, `zig_move` and `zig_rename`.
- **On a live site:** a signed script, run by the site itself
  (`script_route`, `zig build shale-sign`):
  [angry-gopher](https://github.com/showell/angry-gopher) (lynrummy.com) and
  [dog-park](https://github.com/showell/dog-park), on Linux and on
  [zig-metal-unikernel](https://github.com/showell/zig-metal-unikernel). The
  two name `.shale` in their `build.zig.zon`s; the kernel takes `-Dshale-root`.
  The installed suite (`shale_suite`) is built and tested, and no site runs
  it yet. Every effect goes through zig-webbox's `store`, so a script does
  the same on Linux and on metal.

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

- The path must be absolute, and `zig build` must have run first: **the gate
  fails closed**, so without `zig-out/bin/shale-gate` every Bash call is
  refused.
- **To turn it off,** remove that block (Claude Code's `/hooks` menu shows
  it).
- Subagents Claude starts are not gated; only the session itself is.

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
([docs/SHALE-WANTS.md](docs/SHALE-WANTS.md) is ours). `cookbook/wants.shale`
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

## The modules

| module | root | what |
|---|---|---|
| `shale` | `src/shale.zig` | portable: what an application's code imports |
| `shale_linux` | `src/shale_linux.zig` | the Linux host (`run`, jobs, mounts, the Zig tools), for a Linux `main` only |

Both take webbox's `webbox` and `host_io` modules: this repo depends on
zig-webbox by path (`../zig-webbox`), and an application takes `shale` from
`b.dependency("shale", ...)` beside its `webbox`. zig-metal-unikernel
compiles the portable half with its own `host_io`, as it does webbox.

**In a browser:** `zig build web` builds the portable half to WebAssembly
over an in-memory tree (`src/wasm_io.zig`, the `host_io` of
`src/host_io_wasm.zig`; the exports in `src/shale_wasm.zig`) and installs
`zig-out/web/`: `index.html`, a playground (a file tree, an editor and the
run's output, over a sample Python project and scripts to run on it,
`web/sample.js`), and `tutorial.html`, fifteen lessons that build a small
blog a script at a time (`web/lessons.js`).
Serve that folder over HTTP (a page cannot load wasm from `file:`). There is
no `host` there, so `run`, git and jobs are not in it.

    script.run(io, backing_allocator, root, source, budgets, now, out) -> Result

**The reference is [docs/SHALE.md](docs/SHALE.md):**
- the language and its grammar;
- every builtin, with its cost and its errors;
- the budgets and what each charges;
- the store and its confinement;
- the runner (`zig build script`) and the signed route on the live site;
- a first script, step by step;
- the site examples, operator scripts each tested against a store shaped
  like angry-gopher's data;
- what was decided and refused.

## Checks

    zig build test          # every unit test, none skipped; the hunt compiled: about 60 s
    zig build consumers     # the applications' own checks against this tree: minutes
    zig build fuzz          # the expensive fuzzes and sweeps, for a bug hunt
    zig build doc-check     # every repo's docs: each cited path and name is there
    zig build script-coverage   # the lines no unit test runs, per key module: about 2 min

**`test`** runs the two modules' tests, the signer's and the gate's, and
`web-test`: the browser build under Node (`web/smoke_test.mjs`), its
sample's scripts and every lesson's cell, each held to the output it pins. The
Zig tools' tests read zig-webbox's checkout too: they replay splits from its
history and walk its sources beside this repo's. **`consumers`** runs this
repo's own docs check, metal-vmm's queue tool by this tree's `shale`, then
dog-park's `zig build test`, zig-metal-unikernel's `zig build gopher` and
angry-gopher's `ops/check_zig`, one at a time; it needs those checkouts
beside this one, and fails naming one that is missing. Run it before a change
here goes to master. **`fuzz`** runs the hunt: the tests named `expensive fuzz: ...` or
`expensive sweep: ...`, in build.zig's `hunt_files` alone, reached from
`src/script/hunt.zig` and run by `tools/hunt_runner.zig`, which fails on any
test that does not pass (a skip among them) and prints how many ran. `test`
compiles the hunt but never runs it, and skips nothing:
`src/script/hunt_guard_test.zig` fails on a skip anywhere in `src/` or
`tools/`, or a hunt outside the list. A fuzz hunts bugs once, and each bug
it finds becomes a test of its own. **`script-coverage`** runs the coverage SDK's `linecov` (a lazy dependency on
`../zig-coverage-sdk`, which nothing else needs) over the two unit binaries,
built Debug, for every file of build.zig's `coverage_files`; `shale
tools/coverage.shale` reads it back as a table of each module's lines, ran,
uncovered and exempt (`// coverage: not run, <why>` on the line), and fails
when an uncovered count differs from `coverage/lines.txt`, the floor a
change moves in the same commit (`floor=write`). **`doc-check`** runs
`tools/doc_check.shale` from the folder that holds the repos, its fixtures
first: in every repo's tracked docs but the history, each backticked or
linked path must resolve, and each `` `file`'s `name` `` must be in that
file. Each repo's gate runs it for its own docs (`-- only=<repo>`).

`-Dtest-filter=TEXT` runs only the tests whose names contain it;
`-Dtest-timing` prints each test's time, slowest first; `-Dscript-fuzz=N`
sets how many sources the script fuzzer runs. Two more steps measure and
are run by hand, by no tier: `zig build script-bench` (Shale's hot paths)
and `zig build shale-scale` (the largest site each site example fits).

## Docs

- [docs/SHALE.md](docs/SHALE.md): the reference (above).
- [docs/SHALE-AGENTS.md](docs/SHALE-AGENTS.md): Shale as an agent's tool on
  Linux.
- [docs/SHALE-WANTS.md](docs/SHALE-WANTS.md): the ledger of what Claude
  reached past Shale for.
- [cookbook/zig/README.md](cookbook/zig/README.md): recipes for finding a
  Zig file's seams, moving code along them and judging the move, each
  tested to parse. Recipes, not builtins: copy one and change it.
- [cookbook/roc/README.md](cookbook/roc/README.md): recipes for a Roc
  tree: long lines laid out by `roc fmt`, and two archives judged the same
  machine code.
- [docs/designs/DESIGN-shale-stream.md](docs/designs/DESIGN-shale-stream.md):
  a background run's output streamed as it runs; proposed, parked.

## Status

| | |
|---|---|
| Source | its history before this repo is zig-webbox's (`git -C ../zig-webbox log -- src/script`) |
| Runs | on the box, as every agent's shell; in angry-gopher and dog-park on Linux and metal, through their builds |
| Tests | the modules' unit tests; the request-edge harness for its number parses runs in angry-gopher |
