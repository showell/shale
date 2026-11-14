# The cookbook

Each file here is a recipe: a Shale script general enough for any project.
Most you run, with their values on the launch line; a few are libraries,
which a script loads with `use("cookbook:<path>")` and then calls. A recipe
you run is named for what it does, verb first (`check_disk_space.shale`); a
library is named for what it holds (`merge_conflicts.shale`).
`check_recipe_names.shale` holds every recipe to that, and `zig build test`
runs it.

The launches below go through the `cookbook` mount (a line `cookbook =
/ABSOLUTE/PATH/TO/shale/cookbook` in `~/.config/shale/mounts`), so they work
from any checkout; run a recipe from the root of the repo it works on. Each
recipe's header is its full documentation, and most take `self_test=1`,
which runs it over its plants. A glob or a value with a space goes on stdin
with `--args-stdin`, one `NAME=VALUE` a line.

## For any project

- `build_on_github.shale`: runs `zig build test` on GitHub, not on the box,
  and reports the run. `shale cookbook:build_on_github.shale ref=my-branch`
- `check_conflict_markers.shale`: checks that files hold no git conflict
  markers. `shale cookbook:check_conflict_markers.shale files=a.zig,docs/b.md`
- `check_disk_space.shale`: stops a run when the disk is under a floor of
  free space; `report=1` sizes the folders that grow. `shale cookbook:check_disk_space.shale floor=60`
- `check_recipe_names.shale`: checks that every recipe here is named by the
  standard above; `report=1` lists each break with a suggested name.
  `shale cookbook/check_recipe_names.shale report=1`
- `check_seals.shale`: checks that a Zig sealed value is made only in its own
  file. `shale cookbook:check_seals.shale dir=src seals=id_seal:src/player.zig`
- `check_syntax.shale`: checks each file's syntax by its kind (Zig, Roc, JS,
  Shale, Python, shell). `shale cookbook:check_syntax.shale files=src/a.zig,roc/Walk.roc`
- `color_shale.shale`: colors Shale source as HTML pages.
  `shale cookbook:color_shale.shale files=cookbook/look.shale out=colored`
- `fetch_page.shale`: fetches a page and prints its status and the lines that
  matter, or waits until it says something. `shale cookbook:fetch_page.shale url=https://example.org/version`
- `find_phrase.shale`: finds a phrase in wrapped prose, across line breaks,
  and with `new=` edits it. `shale cookbook:find_phrase.shale find=settled in=docs`
- `findings.shale` (library): `print_findings(summary, lines)`, the findings
  between two copies of their summary. `use("cookbook:findings.shale")`
- `listen.shale`: listens to a roc-chat conversation for the posts meant for
  one agent. `shale cookbook:listen.shale site=https://chat.lynrummy.com conv=3 me=agent:2 key=keys:<file> once=1`
- `look.shale`: shows lines by number, and a literal's lines across folders,
  mounts or a git revision. `shale cookbook:look.shale at=src/io.zig:690-730`
- `merge_conflicts.shale` (library): parses the markers git leaves in a
  conflicted file, so a resolution is a few lines. `use("cookbook:merge_conflicts.shale")`
- `prune_merged_branches.shale`: finds the work branches merged into the
  default branch, and with `delete=1` deletes them. `shale cookbook:prune_merged_branches.shale`
- `read_wants.shale`: reads back the log of shell escapes past the gate, with
  counts by kind, tag and session. `shale cookbook:read_wants.shale since=2026-10-23`
- `read_zulip_topic.shale`: prints a Zulip topic's newest messages.
  `shale cookbook:read_zulip_topic.shale --args-stdin` (site, email, key, stream, topic, num)
- `rename_words.shale`: renames a word across a project's files, whole words
  only. `shale cookbook:rename_words.shale from=fetchUser to=loadUser in=src ext=ts,tsx`
- `run.shale`: runs a command line and prints how each program ended, its
  time and the lines that matter. `shale cookbook:run.shale --args-stdin` (cmd, grep)
- `run_zig_build.shale`: runs `zig build STEP` and prints its verdict without
  the noise. `shale cookbook:run_zig_build.shale step=test`
- `test_times.shale`: tests the time-history library on canned runs.
  `shale cookbook:test_times.shale self_test=1`
- `times.shale` (library): `timed_run`, a stage's time against its recent
  runs. `use("cookbook:times.shale")`
- `write_tree_patch.shale`: turns a changed copy of a folder into one patch.
  `shale cookbook:write_tree_patch.shale old=roc new=<copy>/roc under=roc out=x.patch`

## prose/: a book's prose lints

- `prose/build_term_index.shale`: builds a book's index of terms and reports
  terms used before the chapter that teaches them. `shale cookbook:prose/build_term_index.shale book=book/book.md terms=book/index_terms.txt`
- `prose/find_negatives.shale`: finds the negative sentences a book's voice
  rule rewrites or keeps. `shale cookbook:prose/find_negatives.shale manuscript=book/book.md table=book/negatives.txt`

## roc/: Roc code ([README](roc/README.md))

- `roc/check_boundaries.shale`: asks whether each function earns its boundary
  and each parameter is really one. `shale cookbook:roc/check_boundaries.shale`
- `roc/check_errors.shale`: checks how Roc code reports failure.
  `shale cookbook:roc/check_errors.shale`
- `roc/check_families.shale`: checks that look-alike modules are spelled
  alike and one type name means one thing. `shale cookbook:roc/check_families.shale`
- `roc/check_foreign_code.shale`: runs every check that matters over another
  project's Roc code, our house style left out. `shale cookbook:roc/check_foreign_code.shale`
- `roc/check_name_types.shale`: finds names that disagree with their types.
  `shale cookbook:roc/check_name_types.shale table=name_types.txt`
- `roc/check_stacking.shale`: finds groups laid out against the stacking
  rule, and with `write=1` lays them out by it. `shale cookbook:roc/check_stacking.shale`
- `roc/check_vocabulary.shale`: holds a project to the words it has decided
  on. `shale cookbook:roc/check_vocabulary.shale words=vocabulary.txt`
- `roc/compare_machine_code.shale`: says whether two builds of a Roc archive
  hold the same machine code. `shale cookbook:roc/compare_machine_code.shale old=a.a new=b.a`
- `roc/declared.shale` (library): what a file declares, and a name's words,
  read from its parse tree. `use("cookbook:roc/declared.shale")`
- `roc/find_bare_floats.shale`: finds bare `F64`s in declarations, against a
  table of exceptions. `shale cookbook:roc/find_bare_floats.shale table=bare_floats.txt`
- `roc/find_deep_calls.shale`: finds calls nested three or more deep.
  `shale cookbook:roc/find_deep_calls.shale min=4`
- `roc/find_extra_parens.shale`: finds the parentheses and braceless `if`s a
  code emitter leaves, and with `write=1` rewrites them. `shale cookbook:roc/find_extra_parens.shale`
- `roc/find_unclear_names.shale`: finds names and module docs that say less
  than they should. `shale cookbook:roc/find_unclear_names.shale`
- `roc/find_unused_imports.shale`: finds imports a file never uses.
  `shale cookbook:roc/find_unused_imports.shale`
- `roc/gate_recipe.shale` (library): what the recipes that gate a build
  share, their verdict and their plants' self-test. `use("cookbook:roc/gate_recipe.shale")`
- `roc/groups.shale` (library): Roc's bracket groups and the stacking rule.
  `use("cookbook:roc/groups.shale")`
- `roc/headers.shale` (library): which names a file's header dictates.
  `use("cookbook:roc/headers.shale")`
- `roc/house.shale` (library): the checks that are only our house style.
  `use("cookbook:roc/house.shale")`
- `roc/hunt_names.shale`: hunts for awkward names. `shale cookbook:roc/hunt_names.shale`
- `roc/member_uses.shale` (library): how the type printer resolves names and
  counts each member's uses. `use("cookbook:roc/member_uses.shale")`
- `roc/module_family.shale` (library): how the type printer walks sibling
  modules as one family. `use("cookbook:roc/module_family.shale")`
- `roc/parsed.shale` (library): parses a tree's .roc files and fails on the
  ones that don't parse. `use("cookbook:roc/parsed.shale")`
- `roc/pinned.shale` (library): the Roc compiler a repo's `roc-version`
  names. `use("cookbook:roc/pinned.shale")`
- `roc/print_module_types.shale`: prints a module's types and every type they
  depend on, without bodies. `shale cookbook:roc/print_module_types.shale module=Pigs`
- `roc/rank_boundary_hints.shale`: ranks hints that one module does
  another's work. `shale cookbook:roc/rank_boundary_hints.shale root=safari`
- `roc/rename.shale`: renames one identifier across a tree of Roc code.
  `shale cookbook:roc/rename.shale from=portion to=share module=Pie`
- `roc/rewrite_method_calls.shale`: rewrites `List.map(xs, f)` calls as method
  calls, judged by the compiler. `shale --undo-on-stop cookbook:roc/rewrite_method_calls.shale apply=1`
- `roc/types.shale` (library): every expression's type, from Roc's own type
  checker. `use("cookbook:roc/types.shale")`
- `roc/wrap_long_lines.shale`: shortens long Roc lines by letting `roc fmt`
  lay out their groups. `shale cookbook:roc/wrap_long_lines.shale dir=roc width=100`

## go/: Go code ([README](go/README.md))

- `go/find_deep_calls.shale`: finds calls nested three or more deep.
  `shale cookbook:go/find_deep_calls.shale min=4`
- `go/find_smells.shale`: finds long functions, deep blocks, long parameter
  lists and `bool` parameters. `shale cookbook:go/find_smells.shale`
- `go/hunt_names.shale`: hunts for awkward names. `shale cookbook:go/hunt_names.shale tests=0`
- `go/parsed.shale` (library): Go's parse trees and the plants' self-test.
  `use("cookbook:go/parsed.shale")`

## zig/: Zig code ([README](zig/README.md))

- `zig/add_missing_pubs.shale`: builds, and adds the `pub`s the compiler asks
  for after a move. `shale cookbook:zig/add_missing_pubs.shale`
- `zig/check_comments_only.shale`: checks that a change touches comments
  only. `shale cookbook:zig/check_comments_only.shale rev=HEAD~1`
- `zig/check_const_unchanged.shale`: checks that a constant holds the same
  bytes after a refactor. `shale cookbook:zig/check_const_unchanged.shale path=src/x.zig name=page`
- `zig/check_same_tests.shale`: checks that a refactor kept the test suite,
  test by test. `shale cookbook:zig/check_same_tests.shale mode=baseline`
- `zig/check_stacking.shale`: finds groups of three or more items on one
  line, and with `write=1` stacks them. `shale cookbook:zig/check_stacking.shale`
- `zig/count_comments.shale`: counts how much each file's comments say, and
  how loudly. `shale cookbook:zig/count_comments.shale top=10`
- `zig/find_bool_flags.shale`: finds `bool` parameters that calls pass as bare
  literals. `shale cookbook:zig/find_bool_flags.shale`
- `zig/find_clones.shale`: finds the same code in two places.
  `shale cookbook:zig/find_clones.shale window=6`
- `zig/find_dead_code.shale`: finds declarations nothing uses.
  `shale cookbook:zig/find_dead_code.shale`
- `zig/find_repeated_params.shale`: finds runs of parameters that function
  after function declares. `shale cookbook:zig/find_repeated_params.shale min=3`
- `zig/find_seams.shale`: finds where a file divides.
  `shale cookbook:zig/find_seams.shale path=src/x.zig`
- `zig/find_smells.shale`: finds the places a tree may want a second look.
  `shale cookbook:zig/find_smells.shale`
- `zig/flip_bools.shale`: flips each bare `true` or `false` a call passes and
  runs the tests. `shale --undo-on-stop cookbook:zig/flip_bools.shale`
- `zig/hunt_alignment_math.shale`: hunts a compiler for copied alignment
  arithmetic. `shale cookbook:zig/hunt_alignment_math.shale`
- `zig/hunt_names.shale`: hunts for awkward names. `shale cookbook:zig/hunt_names.shale`
- `zig/judge_move.shale`: judges that a move of declarations changed
  nothing. `shale cookbook:zig/judge_move.shale mode=baseline bin=zig-out/bin/x`
- `zig/list_long_files.shale`: lists the .zig files over a thousand lines in
  the repos. `cd ~/showell_repos && shale cookbook:zig/list_long_files.shale`
- `zig/map_file.shale`: prints a map of a file's declarations and tests.
  `shale cookbook:zig/map_file.shale path=src/x.zig`
- `zig/plan_function_cut.shale`: says what a piece of a long function would
  need as a function of its own. `shale cookbook:zig/plan_function_cut.shale path=src/x.zig fn=run first=40 last=80`
- `zig/rename_member.shale`: renames a declaration with its callers across a
  tree. `shale cookbook:zig/rename_member.shale file=src/x.zig from=old to=new`
- `zig/rewrite_call_args.shale`: regroups every call's arguments by a
  template. `shale --undo-on-stop cookbook:zig/rewrite_call_args.shale --args-stdin` (name, template)
- `zig/split_file.shale`: moves declarations to a new file and makes the old
  one use them. `shale --undo-on-stop cookbook:zig/split_file.shale --args-stdin` (from, to, as, names, doc)
- `zig/tidy_file.shale`: tidies a file's imports and empty banners after a
  move. `shale cookbook:zig/tidy_file.shale path=src/x.zig`

## zig17/: the Zig 0.16 to 0.17 upgrade ([README](zig17/README.md))

- `zig17/count_upgrade_changes.shale`: counts what each repo uses that 0.17
  changed. `cd ~/showell_repos && shale cookbook:zig17/count_upgrade_changes.shale`
- `zig17/find_hidden_decls.shale`: finds declarations 0.17's `@hasDecl` no
  longer sees. `shale cookbook:zig17/find_hidden_decls.shale dir=.`
- `zig17/rewrite_array_mult.shale`: rewrites array multiplication as
  `@splat`. `shale cookbook:zig17/rewrite_array_mult.shale dir=src`
- `zig17/rewrite_renamed_calls.shale`: rewrites the calls 0.17 renamed or
  reshaped. `shale cookbook:zig17/rewrite_renamed_calls.shale dir=src`

`words/` holds the word tables the name hunts read, and each `plants/`
folder holds a recipe's test cases.
