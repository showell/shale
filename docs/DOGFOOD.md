# Dogfood log

Each line is a place where Shale got in the way of real work, found while
sweeping code with it, and the commit that fixed it. The sweeps are the
vehicle; these fixes are the yield.

| Found | Friction | Fixed by |
|---|---|---|
| 2026-11-18 | docs/SURFACE.txt went stale when `grep_tree` became a host call, and no build tier ran tools/surface.shale, so nothing caught it | the roc-ast move regenerated it; on 2026-11-18 the file and its tools were deleted, and the goldens hold the texts |
| 2026-11-18 | `git_worktree_add` can't make a new branch from a rev (`{branch, new, rev}` is refused), and a branch made by hand tracks origin/master, so a bare `git_push` pushes there | open |
| 2026-11-18 | a `-- budget:` line inside a heredoc's long string stops the run as a misplaced budget line | open |
| 2026-11-18 | a stop's line is cut at about 180 characters, hiding the why and the offending line | open |
| 2026-11-18 | `run` cuts each output line at a default width, so a long PASS line comes back ending in `…` | open |
| 2026-11-18 | `cookbook:` is the box's master checkout even inside a lane, so a lane's own library edits are invisible to its scripts without a hand-made mounts file | open |
| 2026-11-18 | nothing shows which builtins spend a run's steps; finding a costly regex took ad-hoc profiling | open |
| 2026-11-18 | `nice` needs a `want` on every `run`, though the box's rule is to nice every heavy job | open |
| 2026-11-18 | `grep_tree` takes one needle, so 25 patterns meant 25 walks of the tree and a raised reads budget | `grep_tree` takes a list of needles, one walk (builtins lane) |
| 2026-11-18 | `print` of a list of strings stops the run instead of printing the lines | `print(l)` prints each string on its own line (builtins lane) |
| 2026-11-18 | hunt_names `phrase_name` doesn't see an unqualified type declared inside the module's own type, though its header cites that case as passing | open |
| 2026-11-18 | hunt_names `singular_list` flags a changed copy whose object is the list's element (`set_run(runs, ...) -> runs`) and suggests a misleading name | open |
| 2026-11-18 | check_name_types `no_value` doesn't see through an alias of a function type | open |
| 2026-11-18 | hunt_names `world_verb` can't be answered in the code (no `##` doc or `single`-style answer), so each gate builds its own exception table | open |
| 2026-11-18 | wrap_long_lines `tokens=6` can leave lines past 100 columns that check_stacking then flags, and odd layouts | its wide-line fallback was a second scanner that stacked a function type's arguments, which broke `roc fmt` on safari; it now uses groups.shale's reading, and lists each wide line it can't stack (recipe-leftovers) |
| 2026-11-18 | wrap_long_lines takes one `dir=`, and prints every wide-string line it leaves as noise | `dir=a,b`, `write=0` for a report, and wide strings and data lines counted, not listed (recipe-leftovers) |
| 2026-11-18 | most Roc recipes take one glob and can't be limited to git-tracked files, so a gate must hold file counts itself | open |
| 2026-11-18 | the `── time:` line's `…` cut splits a UTF-8 character, leaving invalid bytes in the output | the C's `host_keep_ends` cuts on a character (seen by frictions-2) |
| 2026-11-18 | rewrite_method_calls stops the whole run on one file no root reaches, instead of listing it and going on | open |
| 2026-11-18 | rewrite_method_calls holds every root's types at once: 8 roots ran out of the 2 GB arena, and the stop doesn't say which root | open |
| 2026-11-18 | `run`'s `re` filter charges the caller's step budget, undocumented; filtering a big child output stopped the caller | open |
| 2026-11-18 | rewrite_method_calls leaves imports it made unused and lines past 100 columns that the gates then catch | it reports `made_unused_import` and `made_long_line` in the same run, by roc-ast's trees before and after (recipe-leftovers) |
| 2026-11-18 | wrap_long_lines stacks a list pattern in a `match` arm because a string widened the line | a group stacks for width only when stacking it brings the line within the width (recipe-leftovers) |
| 2026-11-18 | check_boundaries `constant` can't be answered in code, unlike `single` | open |
| 2026-11-18 | roc-apps' quality.shale caches per checkout path, so every lane starts cold (about 205 s), and it can't be limited to some apps | open |
| 2026-11-18 | quality.shale doesn't pass each project's own tables and exemptions, so its counts differ from Safari's own lints | open |
| 2026-11-18 | quality.shale's findings log leaves out rewrite_method_calls' per-call findings | open |
| 2026-11-18 | the gate refuses a bare `2>&1` after a `shale` launch | open |
| 2026-11-18 | **rewrite_method_calls changed behaviour with `roc check` clean:** `I64.to_str(x)` became `x.to_str()` where `x` was a loop variable over a literal list or a `var` starting from a literal, so it defaulted to Dec and printed `10.0`. The recipe guards a literal argument or a name bound to one, not these | open |
| 2026-11-18 | `grep_tree`'s `skip` stops on a folder that doesn't exist (`c/out` in a fresh lane) while `walk_all`'s `skip` doesn't: the two disagree | a missing `skip` passes (frictions-2) |
| 2026-11-18 | the 1 MiB `kept_bytes` at a `yield` stops a whole-tree recipe that keeps a graph across files; the docs' advice (yield each file) assumes one file's memory | open |
| 2026-11-18 | `unique()` fails on a list of integers, because `has_key` takes strings only | open |
| 2026-11-18 | `mkdir("a/b/")` is refused for its trailing slash | open |
| 2026-11-18 | `scratch:` is the shared temp root, not the session's scratchpad, so stray files land at its top; testing a lane with `SHALE_MOUNTS` also needs an outer heredoc, since the gate allows no env prefix | open |
| 2026-11-18 | after a heredoc's closing tag, a second command is refused with "the heredoc has no last line", which doesn't say what went wrong | open |
| 2026-11-18 | no `range()`, so a counted loop is a hand-written `while` | open |
| 2026-11-18 | a script can't read the arena's bytes in use: `budget().arena_bytes` counts whole chunks | open |
| 2026-11-19 | a script's own `line_at` silently shadows the builtin, but a library calling it gets the builtin and a puzzling type stop | a `function` may not take a builtin's or a prelude's name: the parse stops at its line, naming the clash (no-builtin-shadow) |
| 2026-11-18 | bench/baseline.shale has no workload filter, and a not-clean run writes TSVs into the tracked tree | open |
| 2026-11-18 | `c/build.shale binary=release to=` takes an OS path, so a Shale path (`to=scratch:x/y`) made a folder named `scratch:x` inside the checkout, which Shale's own paths then refuse to name or remove | `to=` takes a Shale path (099cf32); one naming the build's own binary passes (frictions-2) |
| 2026-11-18 | a docs golden is named by its line, so a paragraph added to SHALE-AGENTS.md moves every case below it, each to be named in `refreeze=` (7 for one paragraph) | open |
| 2026-11-18 | a prelude function can't tell a string from a list (no type test, and `==` on a list stops), so an argument that may be either is checked in the host | open |
| 2026-11-18 | `read_lines` under a call-bytes cap of 64 KiB or less stops on any part that doesn't end at a newline: the part is the cap, so its line is "past the cap" at once | open |
| 2026-11-18 | the full tier's per-block step budgets assume few plants re-run; a change to a core file re-runs all of a block's plants and goes over (the store block went to 100M) | open |
| 2026-11-18 | `print` of a flat map stops the run instead of printing it | `print(m)` prints each key and value on a line (frictions-2) |
| 2026-11-18 | `copy()` drops the source's permission bits, so a copied script is no longer executable | each file keeps its mode, host `copy_mode` (frictions-2) |
| 2026-11-18 | a script past the 64 KiB source budget stops as `StreamTooLong`, not as the budget | it stops on `budget source` (frictions-2) |
| 2026-11-18 | `run`'s `whole = true` says only "must be an integer"; an output past `whole = 1` doesn't say `whole` is a byte limit | both say so (frictions-2) |
| 2026-11-18 | a plant whose text is missing says "not in the foundation" whatever its file | it names the file (frictions-2) |
| 2026-11-18 | `keep_cut` cuts a line past KEEP_LINE_MAX inside a UTF-8 character | it backs off to a boundary (frictions-2) |
| 2026-11-18 | check_discipline spends ~84M of its 100M steps, 44M of them on two declaration patterns run on every line with `,` or `=` | type read first, then the patterns on the rest: 52M (frictions-2) |
| 2026-11-18 | `job_start`'s and `job_wait`'s option stops say "`run`:", and `job_start`'s argv stops say less than `run`'s | each names its builtin, one argv wording (frictions-2) |
| 2026-11-18 | docs/SURFACE.txt promised the C matched the Zig byte for byte | a record, no promise; the goldens judge (frictions-2) |
| 2026-11-18 | `git_log` can't be limited to paths | `{paths}` (frictions-2) |
| 2026-11-18 | quadratic `s = s .. x` in a loop | not reproduced: steps, arena and time grow linearly to 200k appends, a field's too (frictions-2) |
| 2026-11-18 | a `grep_tree` regex alternation of 8 literals costs 59M steps over c/, against 10M as 8 regexes and 0.3M as 8 literals: each start spawns a thread per branch, and the first-byte skip barely skips | open: needle lists are the workaround; a fix is a literal prefilter in c/script/regex.c |
