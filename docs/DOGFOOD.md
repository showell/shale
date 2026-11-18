# Dogfood log

Each line is a place where Shale got in the way of real work, found while
sweeping code with it, and the commit that fixed it. The sweeps are the
vehicle; these fixes are the yield.

| Found | Friction | Fixed by |
|---|---|---|
| 2026-11-18 | `docs/SURFACE.txt` went stale when `grep_tree` became a host call, and no build tier runs `tools/surface.shale`, so nothing caught it | the roc-ast move regenerated it; the tier gap is open |
| 2026-11-18 | `git_worktree_add` can't make a new branch from a rev (`{branch, new, rev}` is refused), and a branch made by hand tracks origin/master, so a bare `git_push` pushes there | open |
| 2026-11-18 | a `-- budget:` line inside a heredoc's long string stops the run as a misplaced budget line | open |
| 2026-11-18 | a stop's line is cut at about 180 characters, hiding the why and the offending line | open |
| 2026-11-18 | `run` cuts each output line at a default width, so a long PASS line comes back ending in `…` | open |
| 2026-11-18 | `cookbook:` is the box's master checkout even inside a lane, so a lane's own library edits are invisible to its scripts without a hand-made mounts file | open |
| 2026-11-18 | nothing shows which builtins spend a run's steps; finding a costly regex took ad-hoc profiling | open |
| 2026-11-18 | `nice` needs a `want` on every `run`, though the box's rule is to nice every heavy job | open |
| 2026-11-18 | `grep_tree` takes one needle, so 25 patterns meant 25 walks of the tree and a raised reads budget | open |
| 2026-11-18 | `print` of a list of strings stops the run instead of printing the lines | open |
| 2026-11-18 | hunt_names `phrase_name` doesn't see an unqualified type declared inside the module's own type, though its header cites that case as passing | open |
| 2026-11-18 | hunt_names `singular_list` flags a changed copy whose object is the list's element (`set_run(runs, ...) -> runs`) and suggests a misleading name | open |
| 2026-11-18 | check_name_types `no_value` doesn't see through an alias of a function type | open |
| 2026-11-18 | hunt_names `world_verb` can't be answered in the code (no `##` doc or `single`-style answer), so each gate builds its own exception table | open |
| 2026-11-18 | wrap_long_lines `tokens=6` can leave lines past 100 columns that check_stacking then flags, and odd layouts | open |
| 2026-11-18 | wrap_long_lines takes one `dir=`, and prints every wide-string line it leaves as noise | open |
| 2026-11-18 | most Roc recipes take one glob and can't be limited to git-tracked files, so a gate must hold file counts itself | open |
| 2026-11-18 | the `── time:` line's `…` cut splits a UTF-8 character, leaving invalid bytes in the output | open |
| 2026-11-18 | rewrite_method_calls stops the whole run on one file no root reaches, instead of listing it and going on | open |
| 2026-11-18 | rewrite_method_calls holds every root's types at once: 8 roots ran out of the 2 GB arena, and the stop doesn't say which root | open |
| 2026-11-18 | `run`'s `re` filter charges the caller's step budget, undocumented; filtering a big child output stopped the caller | open |
| 2026-11-18 | rewrite_method_calls leaves imports it made unused and lines past 100 columns that the gates then catch | open |
| 2026-11-18 | wrap_long_lines stacks a list pattern in a `match` arm because a string widened the line | open |
| 2026-11-18 | check_boundaries `constant` can't be answered in code, unlike `single` | open |
| 2026-11-18 | roc-apps' quality.shale caches per checkout path, so every lane starts cold (about 205 s), and it can't be limited to some apps | open |
| 2026-11-18 | quality.shale doesn't pass each project's own tables and exemptions, so its counts differ from Safari's own lints | open |
| 2026-11-18 | quality.shale's findings log leaves out rewrite_method_calls' per-call findings | open |
| 2026-11-18 | the gate refuses a bare `2>&1` after a `shale` launch | open |
| 2026-11-18 | **rewrite_method_calls changed behaviour with `roc check` clean:** `I64.to_str(x)` became `x.to_str()` where `x` was a loop variable over a literal list or a `var` starting from a literal, so it defaulted to Dec and printed `10.0`. The recipe guards a literal argument or a name bound to one, not these | open |
| 2026-11-18 | `grep_tree`'s `skip` stops on a folder that doesn't exist (`c/out` in a fresh lane) while `walk_all`'s `skip` doesn't: the two disagree | open |
| 2026-11-18 | the 1 MiB `kept_bytes` at a `yield` stops a whole-tree recipe that keeps a graph across files; the docs' advice (yield each file) assumes one file's memory | open |
| 2026-11-18 | `unique()` fails on a list of integers, because `has_key` takes strings only | open |
| 2026-11-18 | `mkdir("a/b/")` is refused for its trailing slash | open |
| 2026-11-18 | `scratch:` is the shared temp root, not the session's scratchpad, so stray files land at its top; testing a lane with `SHALE_MOUNTS` also needs an outer heredoc, since the gate allows no env prefix | open |
| 2026-11-18 | after a heredoc's closing tag, a second command is refused with "the heredoc has no last line", which doesn't say what went wrong | open |
| 2026-11-18 | no `range()`, so a counted loop is a hand-written `while` | open |
| 2026-11-18 | a script can't read the arena's bytes in use: `budget().arena_bytes` counts whole chunks | open |
| 2026-11-18 | bench/baseline.shale has no workload filter, and a not-clean run writes TSVs into the tracked tree | open |
| 2026-11-18 | the full tier's per-block step budgets assume few plants re-run; a change to a core file re-runs all of a block's plants and goes over (the store block went to 100M) | open |
