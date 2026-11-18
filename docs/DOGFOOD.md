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
