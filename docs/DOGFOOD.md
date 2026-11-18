# Dogfood log

Each line is a place where Shale got in the way of real work, found while
sweeping code with it, and the commit that fixed it. The sweeps are the
vehicle; these fixes are the yield.

| Found | Friction | Fixed by |
|---|---|---|
| 2026-11-18 | `docs/SURFACE.txt` went stale when `grep_tree` became a host call, and no build tier runs `tools/surface.shale`, so nothing caught it | the roc-ast move regenerated it; the tier gap is open |
