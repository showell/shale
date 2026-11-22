# roc_platform: Roc scripts on C Shale

**Long-lived Shale recipes may be written in Roc**, on this platform: typed,
compiled, tested, and confined exactly as a Shale script is. One-offs stay
in Shale. Its surface is generated from Shale's builtin table.

## How it works

- **The platform** (`platform/Shale.roc`) gives a Roc app Shale's effects:
  files, globs, `run!`, `print!`, `arg!`, `fail!`, `roc_tree!` and the
  rest, each listed there with its type. A Roc app reaches the system only
  through the platform, so every effect it makes goes through one of
  these. **Only effects are exported:** lines, sorting and text are Roc's
  own. **The one exception is the regular expressions** (`re_find!` and
  its kin), which Shale supplies because Roc has none. A hosted function is
  effectful in Roc, so a Roc function that calls one is effectful too.
- **The host** (`host/host.c`) is C Shale itself: it includes `c/shale.c`
  whole, so the command line, the mounts, the preludes, the budgets, the
  report and the exit code are `shale`'s. It runs a small Shale driver, a
  loop that asks the Roc for its next effect (`host("roc_step", answer)`),
  does it with Shale's own builtin and hands the answer back. Each hosted
  function is a shim: it writes its arguments, switches to Shale's stack
  (the Roc runs on a ucontext stack of its own), and returns the answer
  when Shale switches back. So the builtins written in Shale (`run`,
  `glob`, `grep_all`) keep their policy in one home, the preludes.
- **A refusal is a stop, and an outcome is a value.** A path out of the
  root, a program `run` refuses or a budget run out stops the run, in
  Shale's own words. A missing file is `Missing`, and a program's exit code
  is a field. A keep is a tag (`Head(n)`, `Tail(n)`, `Whole(n)`), and
  `run!`'s timeout is required.
- **Roc's memory is an arena of Shale's** (`c/foundation/arena.h`), held to
  the run's `arena_bytes`: `roc_dealloc` frees nothing, and a refusal stops
  the run as `budget arena`. `roc_crashed` stops it with Roc's message.
- **The Roc's own computation has a CPU budget**, `roc_cpu_ms` (`## budget:
  roc_cpu_ms=N` in the script, or `--budget roc_cpu_ms=N`). Steps count
  Shale's work, so the Roc's is timed instead: a timer on the thread's CPU
  clock, armed while the Roc runs, stops a loop that never calls Shale. A
  runaway stops as `budget cpu`, at its own line, and the `── used:` line
  gives the Roc's part.
- **A stop made while the Roc waits names the Roc's file and line**: a
  hosted call Shale refused, a `fail!`, a budget run out answering one, the
  CPU budget: `` ── stopped at roc_platform/examples/Reach.roc:10:7 (in
  `Shale.read!`): argument: ... ``. Each shim keeps its return address; the
  launcher links the program with a table of the lines of the script's
  files (`app.sites`, from the pinned Roc's DWARF line table, which names a
  row's file by its module), and the address's file and line are the
  stop's. The column is the call's own text on that line, else the line's
  first.
- **A value no `Shale.arg!` reads stops the run** before anything is built,
  naming the ones the script reads, as Shale does for a script's `arg`.

## Generated from the builtin table

`c/builtins.txt` is the one home of Shale's builtins
(`c/docs/builtin-table.md`). An effect it exports gives its Roc signature,
which outcomes are values and its driver clause, and
`c/gen_builtins.shale` writes from it `platform/Shale.roc`, the hosted
block of `platform/main.roc`, `host/hosted.gen.c` and `surface.gen.shale`.

Change the table and run `shale c/gen_builtins.shale`; leave those four
files to the generator. The C types are `roc glue`'s
(`host/roc_platform_abi.h`, written by `tools/glue.shale` with the pinned
Roc), so a change to what is exported is three runs: `gen_builtins`,
`tools/glue.shale`, `gen_builtins` again (glue names some types by their
order in Shale.roc). The fast tier (`c/build/builtin_table.shale`) holds
the generated files and glue's prototypes to the table, and has GCC read
`host.c` with `-Werror`.

## Running a script

    shale roc_platform/run.shale script=roc_platform/examples/count_lines.roc file=README.md

Run this from the shale checkout's root. `run.shale` builds the script with
the pinned Roc (`roc-version`; `opt=speed` for `--opt=speed`) into an
archive and links it with the host's object, which is all of C Shale
compiled once with `gcc -O2`. The launcher then runs the program in this
root, or rooted at `in=` (an absolute folder). The object and each
script's program are cached by content hash under `out/`, each built in a
job of its own (`out/jobs/`), so concurrent runs build a key once. A
`## budget: NAME=N ...` line in the app's root sets its budgets.
`run.shale`'s header has the rest of its arguments.

**A script may be several files.** `script=` names the app's root, and the
launcher takes with it:

- **its own modules**, each a type module beside the file that imports it
  bare (`import Words` is `Words.roc`), followed in turn. A module reaches
  the system as the root does, through `import pf.Shale`: the platform is
  the only door, whichever file asks.
- **packages by a relative path** in the app's header (`lib:
  "../lib/main.roc"`, then `import lib.Plural`), as roc-chat imports
  roc-markdown: that `package [Plural] {}` file and the modules it
  exposes. This is how the recipes share their modules. The cookbook's
  Roc ports name `cookbook/roc/lib/main.roc` in their headers as `lib`, then
  `import lib.Tree`, and its Tree, Declarations, Text and Finding were each
  a copy in several recipes. The recipes over Shale's C name their splitter,
  `cookbook/c/items/main.roc`, as `items`. A package reaches no effect, so
  what runs a program or reads a file (a self-test, a word list, the C
  itself) stays in each recipe, which hands the package the text: so
  check_errors, hunt_names and check_boundaries each keep a small
  `Types.roc`, the same file in all three, that runs roc-types and hands
  its output to the package's `RocTypes`. A script's package imports no
  package of its own.

Every file is read through Shale's store (a path out of the root stops the
launch), copied into the cache (the root as `main.roc`, packages under
`pkg/<name>/`, the header's paths made the cache's) and hashed into the
key, so an edit to any module rebuilds the program and nothing else does.
`Shale.arg!` reads are found in all of them. Two modules of one name are
refused: Roc's line table names a file by its module, and a stop could not
say which. The host's sites table numbers the files (`file K PATH`, and each
row, call and line its `K`), so a stop in a module names that module's
`file:line:col`.

**A script's build prunes `out/`** of what no current key reaches: every
host object but the current one, every build linked with another host's,
the script's own builds at other keys (each edit's) and the logs of the
jobs that made them. A build whose job still runs is left alone. A build
says what it was built for in its `built_for` file, which pruning reads.

**`reads=inputs`** builds and runs nothing: it prints the real path of every
file the program is built from (the script's files, the platform, the
launcher, the host and the code git tracks in `c/`), one a line, for a
caller that keys a cache on a Roc recipe. A launcher hands its values on,
so `shale cookbook:roc/find_unclear_names.shale reads=inputs` asks the
recipe itself, with no `const SCRIPT` to read.

The examples in `examples/`: `count_lines.roc`, the smallest app;
`check_module_size.roc`, a cookbook recipe in Roc, held line for line to
its Shale original by `tools/compare.shale`; and `escape.roc`, which tries
each guard and stops with Shale's own refusal. escape.roc is several files:
`Reach.roc`, a module of its own that tries a guard too, and `kit/`, a
package it names by a relative path.

**`shale roc_platform/self_test.shale`** is the policy proofs: each of
escape.roc's tries held to Shale's own stop or output (a refusal in
Reach.roc named at Reach.roc's line, a package's function run), then a
misspelled argument and a right one. `shale=c/out/tcc/shale` runs the launcher with a
checkout's own build. It needs the pinned Roc and builds the host's
object, so it runs when a Roc build is asked for, outside the tiers.

`tools/measure.shale` times the builds and the runs.

## The ports

**`cookbook/roc/find_unclear_names/`** is a recipe in Roc: its app's root
is `find_unclear_names.roc` (the word lists, the run, the verdict), and
beside it are a module a check (`PureMutator`, `Abbreviation`,
`ModuleDoc`) and `SelfTest`, each under 500 lines; what the checks read
(`Tree`, `Declarations`, `Text`) and `Finding` are `cookbook/roc/lib/`'s.
`cookbook/roc/find_unclear_names.shale` is its launcher. The
launcher hands its values to `run.shale` in the checkout its cookbook is in
(the `cookbook` mount's parent) with `in=` its own root, so the build is
the checkout's and the program runs where the caller is. That folder's
`check_goldens.shale` holds it, output and exit code, to goldens written
from the Shale original on the plants, and the full tier runs it with the
cookbook's self-tests.

**`cookbook/roc/count_roc/count_roc.roc`** is the small one, a file a Roc
reader can take in at once: a Roc project's modules, lines and `expect`s,
folder by folder, from `Shale.glob!` and `Shale.read!` and plain Roc.
`cookbook/roc/count_roc.shale` is its launcher, and hands its `in=` on as
`folders=`, since run.shale's own `in=` is the root. Its
`check_goldens.shale` runs it on a copy of `cookbook/roc/plants/count_roc/`.

**`cookbook/c/map_file/`**, **`cookbook/c/list_long_functions/`**,
**`cookbook/c/list_deep_nesting/`**, **`cookbook/c/find_clones/`**,
**`cookbook/c/find_dead_code/`**, **`cookbook/c/hunt_names/`** (over the
splitter package **`cookbook/c/items/`**, which each names by a relative
path), **`cookbook/zig/list_long_files/`**, **`cookbook/zig/count_comments/`**,
**`cookbook/roc/find_unused_imports/`**, **`cookbook/roc/check_families/`**,
**`cookbook/roc/find_deep_calls/`**, **`cookbook/roc/check_vocabulary/`**,
**`cookbook/roc/check_name_types/`**, **`cookbook/roc/find_bare_floats/`**,
**`cookbook/roc/check_errors/`** (which runs roc-types too, for `types=1`),
**`cookbook/roc/hunt_names/`** (roc-types too),
**`cookbook/roc/check_boundaries/`** (roc-types too),
**`cookbook/count_shale_stops/`**, **`cookbook/roc/find_extra_parens/`**,
**`cookbook/roc/check_stacking/`**, **`cookbook/roc/wrap_long_lines/`** and
**`cookbook/roc/rewrite_method_calls/`** (roc-types too)
are ported the same way: the launcher keeps the recipe's name and arguments, the goldens were written
from the Shale original on plants, and `check_goldens.shale` holds them.
count_comments' launcher builds with `opt=speed`, since its byte-by-byte
scan runs about 60 times slower in Roc's dev build, and so does
find_deep_calls', whose dev build took 1.6 times the Shale original's CPU;
count_shale_stops', which reads a transcript's JSON in Roc (its
ShaleJson module, as Shale's `json_read` reads it) and whose dev build took
3.5 times the original's CPU over a 180 MB transcript; and the six over
the C, whose splitter masks a line a byte at a time (the dev build took
four times map_file's original's CPU over `tree=c`).
find_extra_parens rewrites as well as finds (`write=1`): its goldens hold
the files a rewrite of every plant leaves, and the rewrite reads a file
with no finding no more, since Roc's arena frees nothing and a second
parse of a large tree would not fit (`real!`, `mkdir!` and `remove_tree!`
were exported for it: `roc fmt`'s argv and the self-test's scratch copy).
check_stacking rewrites too, and its goldens hold the same; its reading of
the bracket groups and the stacking rule is the library's `Groups`,
`Stacking` and `Packed` (`cookbook/roc/lib/`), the Shale groups.shale's
line for line, its regular expressions matched by hand. Its dev build took
0.8 times the original's CPU over roc-apps' projects, and runs the whole of
roc-apps at once, where the original runs out of its 2 GB arena.
wrap_long_lines rewrites by the same library, so it is the stacking rule's
one home and groups.shale is gone; its goldens run on copies of the plants
with a `roc-version`, which Shale's checkout has none of. Its dev build
took 0.8 times the original's CPU (`roc fmt`'s runs included) over copies
of roc-chat, roc-screensaver and roc-apps' projects.
rewrite_method_calls rewrites by the compiler's judgement: each trial is
written in place and `roc check` and `roc test` run on it, and its goldens
hold the files each case leaves. A trial is the Roc program's own write, so
the launcher's `--undo-on-stop` does not reach it: a run stopped mid-trial
can leave that file rewritten, for git to put back.
**`cookbook/roc/EXAMPLES.txt`** lists every port, a line each: its Roc, its
Shale original's path and the commit that replaced it, so a page can show
the two side by side. Its sections (the checks on Roc code first) are the
page's; its header says the format.

## Pitfalls

- `RocStr` is `bytes, capacity, length`, while `RocList` is `elements,
  length, capacity`.
- `roc build` takes only a relative platform path, so the launcher writes
  one.
- The dev archive's object lacks `.note.GNU-stack`, so it is linked with
  `-z noexecstack`; the `--opt=speed` archive is not PIE, so it is linked
  with `-no-pie`.
- Shale's `run_new_table` makes a table of no kind, which Shale reads as
  empty. A host-made list must set `TABLE_LIST`.
- TCC can't read glue's header (`__int128`), so the host is built by GCC.
- `module` and `with` are reserved words in Roc: neither names a record's
  field, and the parser's error points past them.

## Open edges

- A crash names the driver's line: `roc_crashed` is called through Roc's
  builtins, so its return address is theirs. So does any stop at an
  address outside the script's own lines.
- A stop names the hosted call and its line, but neither the Roc function
  it is in (Roc's DWARF names its functions by hash) nor the Roc calls
  that led there.
- The launcher's own `fail` adds a second stop line after the program's.
- The Roc's arena has its own budget, set to the run's `arena_bytes`,
  beside the values' arena.
- The host's cache key ignores uncommitted edits under `c/`.
- A stop in a package's module names the driver's line: the dev build's
  line table had no rows of a package's module where it was tried. A
  package reaches no effect, so only a crash or the CPU budget can stop
  there.
- A build of under a second takes about 1.3 s of wall time: the launcher
  waits for its build job by `job_wait`'s polling, which doubles from 20 ms.
- `measure.shale` copies an example's root alone, so it times one-file
  examples only.
