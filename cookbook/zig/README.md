# The Zig cookbook

Recipes for finding a Zig file's structure and moving code along it,
written in Shale over its Zig builtins (`zig_decls`, `zig_tokens`,
`zig_move`, `zig_rename`, `zig_renames`, `zig_imports`: Zig's own
tokenizer and parser). They are recipes, not builtins: read one, copy it,
change it for the job in front of you. Each is tested to parse
(`src/shale_cookbook_test.zig`).

Run one from the checkout it works on. `<shale>` is the shale checkout
; `<repo>` is the repo the recipe works on, its
root the current directory:

    cd <repo> && shale <shale>/cookbook/zig/seams.shale path=src/x.zig

**A recipe says the room it needs.** The tree-wide recipes (`smells`,
`dead`, `bool_calls`, `param_runs`, `clones`) hold each file's tokens for
the run, far past the CLI's defaults (5,000,000 steps). Each says its
budgets in a line among its opening comments,

    -- budget: steps=900000000 arena_bytes=4000000000

which `shale` takes and prints before the run (`── budgets from the
script's header: ...`), so the cost is in sight. A `--budget` on the
command line still wins.

**A value a plain word cannot carry goes on stdin.** The gate passes only
a plain launch (SHALE-AGENTS.md, "The gate"), so a glob
(`glob=src/**/*.zig`), a doc, a test's name or a `|` goes on stdin after
`--args-stdin`, one `NAME=VALUE` a line, raw. Every tree-wide recipe's
`glob=` defaults to `src/**/*.zig`; another is given this way:

    cd <repo> && shale <shale>/cookbook/zig/smells.shale --args-stdin <<'ARGS'
    glob=probe/**/*.zig
    ARGS

A recipe that changes several files runs with `--undo-on-stop`, so a stop
leaves them as they were:

    cd <repo> && shale --undo-on-stop <shale>/cookbook/zig/zig_split.shale --args-stdin <<'ARGS'
    from=src/x.zig
    to=src/x_part.zig
    as=x_part
    names=a|b|the part's test, with a comma
    doc=What the part is.
    ARGS

## Finding the seams

- `seams.shale` (`path=`, `container=Name` for a struct's members,
  `hub=N` (a declaration N use; an eighth of the file's, at least 4),
  `out=N` (one that uses N; a sixth, at least 6), `min=120` (lines each
  side of a gap), `show=10` (gaps), `names=12` (each group's names)): the
  file's reference graph (`zig_decls`' `uses`), the largest declarations,
  the hubs set aside (what much uses, and what uses much), the cut profile
  in file order (the gaps the fewest edges cross), and **each declaration
  grouped by the hubs that reach it**: the structure by caller, which is
  where a hidden seam shows.
- `fn_cut.shale` (`path= fn= first= last=`): what a cut of a long
  function would take (the locals it reads), write and give back: the
  parameters an extracted function would need.
- `file_map.shale` (`path=`): a file's top-level declarations and tests,
  with their lines.
- `long_zig.shale` (`over=N`): every `.zig` file over a thousand lines in
  the repos it names (its list is one box's; change it). It runs from the
  folder holding them (`cd ~/showell_repos`), not from a repo.

## Smells: where a tree may want a second look

Lists, not verdicts, read from Zig's tokenizer (never a comment's or a
string's text). Each says its budgets in its `-- budget:` line:

- `smells.shale` (`glob=`, `tests=yes`, `long=`, `params=`, `top=`): long
  functions (a `fn X(...) type` marked a type, not a function to cut),
  long parameter lists, `bool` parameters and fields, `catch {}` (one in
  another error's handler, a cleanup, said apart) and `catch unreachable`
  by file, and numbered steps in comments (a function's own order is
  fine; a caller's is a protocol that wants to be code).
- `bool_calls.shale` (`glob=`, `glob2=`): each function with a `bool`
  parameter and the calls that pass it a bare `true` or `false`, the ones
  a named enum would make read; **a bool every call passes the same
  literal is no parameter at all** (`ONE VALUE`). A name declared by more
  than one function is said, as its calls cannot be told apart.
- `dead.shale` (`glob=`, `also=`, `drop=yes`): declarations nothing uses,
  which Zig never reports (its analysis is lazy). Dead: not `pub`, and no
  other identifier in its file names it, which is certain. Unreached: `pub`,
  and nothing in the tree names it, which is a question (another repo, a
  build, `@field`). `drop=yes` removes the dead, each by its own bytes; run it
  under `--undo-on-stop`, again until none is left, and let the tests
  judge.
- `param_runs.shale` (`glob=`, `min=`, `longest=`, `skip=`, `top=`): a run
  of parameter names that several functions declare in a row, plumbing
  (`io`, `alloc`, ...) passed by: a struct with its users already, where a
  long list is only a question. Each run once, at its longest.
- `clones.shale` (`glob=`, `tests=yes`, `window=`, `top=`): the same code
  in two places, runs of 6 or more alike lines (trimmed; blank lines,
  comments, imports, one-name aliases and brackets alone passed by), in
  one file or two. A find is a question, not a verdict: code may be alike
  on purpose.
- `comment_census.shale` (`dir=`, `files=`, `top=`): per Zig file, its
  code, doc and plain comment lines, comment lines per 100 of code, words
  a comment line, and the loud and dated comments.

## Moving along them

- `zig_split.shale` (on stdin: `from= to= as= names=a|b doc=`, and
  optionally `after=<import> uses=alias from_as=X reexport=yes into=yes`):
  declarations moved to a new file and the old one made to use them:
  `zig_move`, `pub` where the old file still uses one (a member through
  its container), the new file's doc and imports, the old file's uses
  qualified or aliased (`uses=alias`; a `var` always qualified), the
  import after the last top-level one (or after the one `after=` names),
  the new file in the old one's `test {}` block. `from_as=X`: the moved
  code reaches what it needs of the old file through it. `reexport=yes`: each moved `pub`
  declaration stays the old file's too, for a module whose surface is the
  file itself. `into=yes` joins a file already there. Refuses an import
  name the old file already uses (by `zig_tokens`).
- `pub_loop.shale` (`build=<step>`): builds, and puts `pub` where the
  compiler says a method called through a value needs it, until it builds.
- `call_args.shale` (`name= template= [glob=] [count=]`): every call of a
  function, its arguments regrouped by a template of `$N`s (`$1,
  .{ .dir = $2, .key = $3 }`), the call sites' half of a new signature.
  Arguments split at the call's own commas, past brackets, strings and
  characters. A call that runs past its line, or has other than the
  template's count of arguments, is refused by place, and then nothing is
  written. The declaration is yours to change. Run it before a body's
  own call is changed by hand: a call already in the new shape has the
  wrong count and refuses the run. Its body's half, when
  parameters become one struct's fields, is a builtin, no recipe:
  `zig_renames(path, {stream = "w.stream", ...}, {lines = {first,
  last}})` over the body's lines, by Zig's tokenizer (never a field, a
  string or a comment). It stops at a declaration of a name, so change
  the signature first.
- `zig_tidy.shale` (`path=`): an alias moved after the import it names;
  section banners with nothing under them removed.


## Judging a test suite

- `flip_bools.shale` (`glob=`, `build=test`, `most=40`,
  `timeout_ms=600000` a test run): each bare `true`
  or `false` a production call passes, flipped alone, and the build's tests
  run; a flip that survives is a decision no test checks. A test run per
  flip: a hunt, run detached, never a gate. Read each survivor: code the
  step never runs survives too.

## Judging a move

- `move_judge.shale` (`mode=baseline|check bin=a,b [build=step]
  [flags=-Dstamp=off] [test=none] [clean=yes] [same=old,new,...]
  [new_files=...]`): a
  pure move changed nothing: the tests by name and the runner's count,
  each binary's symbols by name and size (the file part left out, Zig's
  type numbers and coverage sites' hashes normalized). `new_files=`
  excuses exactly one file-name string per new file that holds a coverage
  site, and nothing else. `clean=yes` refuses a tree with changes (a
  stamped build says `+dirty`). A large file's moves need room, which
  its `-- budget:` line gives.
- `same_tests.shale` (`mode=baseline|check`): a checkout's suite again,
  against a baseline of test names, wherever a moved test now lives.
  `mode=baseline` runs `zig build test` before the refactor and writes the
  names to `zig-out/refactor-baseline.txt`; `mode=check` runs it after and
  holds every test passing and the same names.
- `const_judge.shale` (`path= name= [rev=HEAD]`): a top-level `const` as
  `rev` had it and as the file has it now, compiled side by side in a
  throwaway test and compared by Zig, byte for byte: for a page's text
  spliced from a shared part (`a ++ shared ++ b`), which a symbol
  judge cannot see. A planted one-character change fails it.
- `comments_only.shale` (`rev=HEAD`, `files=`): a change that touches
  comments only, by Zig's tokenizer, each file against `rev`; for one
  that does not, the first token that differs.

## Writing a recipe

What a recipe's author meets first, each in `docs/SHALE.md`:

- One name a `local`: `local a, b = ...` is refused ("Refused, each by
  name").
- No `local function` or nested function: a function is `function
  name(...)`, at the top level ("Statements").
- A function sees its parameters, its locals and the `const`s above it,
  never the script's `local`s: what functions share is a top-level `const`
  ("Names").
- Integers only: no floats and no `/`; `//` divides, flooring
  ("Operators").
- `print` takes scalars: a list or a map goes through `join`, `table` or
  its fields ("The host and output").
- `sort` sorts in place and gives the list back ("Lists and maps").
- A regular expression has at most one capture group, `m.group`
  ("Regular expressions").
- A long string always has a level, `[=[ ... ]=]`; one holding `]=]` takes
  `[==[ ... ]==]` ("Values").
