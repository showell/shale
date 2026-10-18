# The Zig cookbook

Recipes for finding a Zig file's structure and moving code along it,
written in Shale over its Zig builtins (`zig_decls`, `zig_tokens`,
`zig_move`, `zig_rename`, `zig_renames`, `zig_imports`: Zig's own
tokenizer and parser). They are recipes, not builtins: read one, copy it,
change it for the job in front of you. Each is tested to parse
(`src/shale_cookbook_test.zig`); what each does is shown by the splits
made with them (the kernel's io.zig, tcp_sim.zig and virtio.zig, metal-vmm's
main.zig, this repo's shale_host_linux.zig, 2026-10-18).

Run one from the checkout it works on:

    cd <repo> && shale <shale>/cookbook/zig/seams.shale path=src/x.zig

A recipe whose values a plain word cannot carry (a doc, a test's name,
`|`) takes them on stdin, one a line, raw, and one that changes several
files runs with `--undo-on-stop`, so a stop leaves them as they were:

    cd <repo> && shale --undo-on-stop <shale>/cookbook/zig/zig_split.shale --args-stdin <<'ARGS'
    from=src/x.zig
    to=src/x_part.zig
    as=x_part
    names=a|b|the part's test, with a comma
    doc=What the part is.
    ARGS

## Finding the seams

- `seams.shale` (`path=`, `container=Name` for a struct's members): the
  file's reference graph (`zig_decls`' `uses`), the hubs set aside (what
  much uses, and what uses much), the cut profile in file order (the gaps
  the fewest edges cross), and **each declaration grouped by the hubs that
  reach it**: the structure by caller, which is where a hidden seam shows.
- `fn_cut.shale` (`path= fn= first= last=`): what a cut of a long
  function would take (the locals it reads), write and give back: the
  parameters an extracted function would need.
- `file_map.shale` (`path=`): a file's top-level declarations and tests,
  with their lines.
- `long_zig.shale` (`over=N`): every `.zig` file over a thousand lines in
  the repos it names (its list is one box's; change it).

## Smells: where a tree may want a second look

Lists, not verdicts, read from Zig's tokenizer (never a comment's or a
string's text):

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
  judge. On 2026-10-18 it found 120 unused imports across five repos, and
  one dead function.
- `clones.shale` (`glob=`, `tests=yes`, `window=`, `top=`): the same code
  in two places, runs of 6 or more alike lines (trimmed; blank lines,
  comments, imports, one-name aliases and brackets alone passed by), in
  one file or two. A find is a question: one became `store.Store.of` in
  the kernel; two simulators alike on purpose stayed apart.

## Moving along them

- `zig_split.shale`: declarations moved to a new file and the old one made
  to use them: `zig_move`, `pub` where the old file still uses one (a
  member through its container), the new file's doc and imports, the old
  file's uses qualified or aliased (`uses=alias`; a `var` always
  qualified), the import after the last top-level one, the new file in the
  old one's `test {}` block. `from_as=X`: the moved code reaches what it
  needs of the old file through it. `reexport=yes`: each moved `pub`
  declaration stays the old file's too, for a module whose surface is the
  file itself. `into=yes` joins a file already there. Refuses an import
  name the old file already uses (by `zig_tokens`).
- `pub_loop.shale` (`build=<step>`): builds, and puts `pub` where the
  compiler says a method called through a value needs it, until it builds.
- `zig_tidy.shale` (`path=`): an alias moved after the import it names;
  section banners with nothing under them removed.

## Judging a move

- `move_judge.shale` (`mode=baseline|check bin=a,b [build=step]
  [flags=-Dstamp=off] [test=none] [same=old,new,...] [new_files=...]`): a
  pure move changed nothing: the tests by name and the runner's count,
  each binary's symbols by name and size (the file part left out, Zig's
  type numbers and coverage sites' hashes normalized). `new_files=`
  excuses exactly one file-name string per new file that holds a coverage
  site, and nothing else. A large file's moves need room: `--budget
  steps=200000000 --budget arena_bytes=...`.
- `same_tests.shale`: a checkout's suite again, against a baseline of test
  names, wherever a moved test now lives.
- `const_judge.shale` (`path= name= [rev=HEAD]`): a top-level `const` as
  `rev` had it and as the file has it now, compiled side by side in a
  throwaway test and compared by Zig, byte for byte: for a page's text
  spliced from a shared part (`a ++ shared.css ++ b`), which a symbol
  judge cannot see. A planted one-character change fails it.
