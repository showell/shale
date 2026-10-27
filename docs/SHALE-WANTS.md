# What Claude reached past Shale for

The ledger of the gate's escapes. The gate (`tools/shale_gate.zig`,
SHALE-AGENTS.md's "The gate") passes only a `shale` launch or a logged
escape. Each escape is a line in `~/.local/state/shale/wants.tsv`
(`cookbook/wants.shale` reads it), and this file gathers the ones still
open: a line is the date, a tag, what was done, and the Shale that would
do it; a tag that recurs three times goes to the queue.

## Wants

| date | tag | what Claude did | the Shale that would do it |
|---|---|---|---|
| 2026-10-17 | doc-check | five cold agents audited every doc by hand, listing checks a script could run every time; `tools/doc_check.shale` (`zig build doc-check`) does the first, every cited path and name resolves | the rest: every tracked doc is linked from the README; a README names no release tag or short hash; no "Steve, DATE" later than its line's commit; each table of names (builtins, knobs, routes, env vars, budgets, `/version` fields, build steps, scripts) equals the code's list |
| 2026-10-17 | default-mounts | couldn't grep zig's std (outside the root), nor `edit` my own scratch scripts | a read-only mount of the zig install by default; the script's own folder readable |
| 2026-10-17 | walk-more-trap | `walk("chat", {like = "*.md"})` ended with a `more` entry whose path was a `.png`; my script took it for a transcript (SHALE.md's checklist says to read `kind` first; I didn't) | a filtered walk's `more` entry still trips a careful author: name it so it can't be taken for a match (a `resume` field on the walk's result, not an entry), or make `like` apply to it |
| 2026-10-19 | roc-tree-diff | rebuilding dog-park's goat.patch after a reformat: `git archive \| tar` and `patch` by bash (cookbook/roc/tree_patch.shale now writes the patch) | a scratch copy of a tree (`copy_tree(from, to)`, or a git ref's) and `apply_patch(dir, patch)` |

## Out of scope

| date | tag | what Claude did | why it is not Shale's |
|---|---|---|---|
| 2026-10-17 | transcript-mine | parsed the session's 310 MB JSONL transcript in python to extract every Bash command and count programs | a one-off analysis of a format Shale has no reason to read (JSON at that size); if it recurs, a zig tool in `tools/` |
| 2026-10-17 | signal-experiment | a bash script that traps and logs signals, with children in their own group and session, to learn how Claude Code ends a timed-out call | an experiment on processes and signals themselves, which Shale deliberately does not expose |
