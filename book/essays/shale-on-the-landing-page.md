# What a landing page taught us about Shale

*2026-11-15. Observations from rebuilding roc.lynrummy.com's landing page: a web project, not a Shale project, which is exactly why it's a good test of Shale.*

## The shape of the work

The page took about an hour of work and dozens of Shale launches. Almost none of them built anything. They gathered facts (lines, modules and `expect`s per project), checked claims before the page made them, ran the site's existing bash scripts, and verified the result on dev, staging and prod. Shale was the agent's notebook and its fact checker, which is the niche it was built for.

## What worked

- **Fact-checking became cheap enough to do every time.** Every "Roc at work" and "plays nice" claim got a one-launch check before it went on the page: whether Safari's union really is declared only in a signature, whether any Safari module still says it was emitted (none of 98), whether Fast Track's search calls the game's own `update_game`, and whether Luke's apps still type-check against roc-ray. Two claims came off the page because a check said no: Safari's native build, and "passes" for BASIC.

- **`report_run` earned its promotion on its first real day.** It ran the site's bash scripts in one line each, with a reason, and printed a verdict the agent could read at a glance. With `cwd` and `keep_going` it made the native type-checks a three-line loop.

- **Mounts did quiet, important work.** `repos:` read roc-ray's license, `build:` reached the dev channel, `tools:` found the pinned Roc, `memory:` read a memory the store refused by absolute path, and `projects:` held the transcripts. None of it needed a path outside the root.

- **The prod deploy carries its reason.** The deploy ran as `bash site/deploy.sh` with the want "the prod deploy of staging, on Steve's sign-off", so the session's record says why prod changed, in words.

## Where it rubbed

- **The 64-program cap, again.** Checking the page's 60-odd links in one run hit the cap and the step budget, because GitHub's pages are large. The fix was two launches by hand. That's the second time in two days, after `map_repos` split itself into child runs.

- **A `..` path, twice.** Both times the agent reached for a sibling folder, and both times the stop was instant and clear. It's a habit, and worth counting in the next census.

- **No copy builtin.** Putting the published playground on dev was a tree copy across mounts, so it went to `cp -r` through `run`. Shale reads, writes and edits files, but it can't copy a tree.

- **A stats script that reads build output can drift.** `book_stats.shale` stopped because the last build of the book no longer matched the check's table, so its numbers would have been stale. The agent counted from the source instead.

- **The regex subset has no counted repeats.** `{8}` stopped with a message naming the fix, and the next launch used a plain string.

## What might promote

| Done by hand today | Could become |
|---|---|
| lines, modules and `expect`s for a Roc project, twice in two days | a recipe, `count_roc.shale` |
| every link on a page, local and outside, checked for a 200 | a recipe, `check_page_links.shale`, in parts as `map_repos` is |
| a tree copied from one mount to another | a builtin, `copy`, that the store confines like every write |
| many small programs in one run | a prelude helper that runs a list of jobs in child runs and joins their answers |

## For the book, and for cadence

This project is the kind the spike's chapters describe: real web work, where Shale never builds the product but makes the agent's work checkable. Every claim on the page is one the agent could back with a launch, and the reviewers' corrections landed because each could be checked in a minute. The census habit fits it too: these rubs join the transcript, and next week's count says which of them repeat.
