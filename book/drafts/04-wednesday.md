# Draft

## Wednesday: a check for each moment

{{quote repos:essay-surface/journal/prompts/wednesday.md}}

By Wednesday the essay surface had four scripts, and Tuesday's report listed what to run and when. A list is something to remember. Today it became two commands.

### Measure, then listen

Monday's numbers had already named the cost: the index checked every note's file on every request. Before touching it, the agent measured the parts of its plan. One survey counted where the 505 real notes keep their dates: nearly always on line 3, in the paragraph after the title. Another timed `git log` on the notes folder at 0.8 s, because that repository held 3,961 loose objects and no pack. That repository is Steve's, so the agent left it alone, and the server pays the cost once, at start-up.

The new server hears changes from the kernel, through inotify, and reads the queue at the start of each request. The kernel queues an event before the write returns, so the next request always sees it. The index page is kept until a note changes. Here is the same bench on the same notes, before and after:

{{fence repos:essay-surface/journal/wednesday.md "median 2.276 ms"}}

{{fence repos:essay-surface/journal/wednesday.md "median 0.105 ms, p90 0.143 ms"}}

The index went from 2.3 ms to 0.1 ms, as fast as a note. A note's date now comes from its first lines, or else from its last commit. A fresh clone resets every file's time, and it gives the same index, all 506 notes in the same place. That comparison became a stage of the full check.

The mistakes on the way stopped loudly. An `edit_between` that moved the store out of `server.go` reported `-175 +3 lines`, and that count showed it had cut the imports too before anyone looked. A guessed builtin, `take_last`, stopped its script at once, with zero steps run.

### A check for each edit

`fast.shale` is the check Steve runs without thinking. Here is its body:

{{lines repos:essay-surface/scripts/fast.shale from "local fmt = stage" to "checks through the real server"}}

Read it as a list of five stages: the formatter, vet, the front end's syntax, every Go test, and a smoke test through the real server. The last two stages count what ran, because a green is a claim: "44 tests passed, 63 subtests" can be checked, and "ok" can't.

Each stage goes through one function:

{{function repos:essay-surface/scripts/fast.shale stage}}

Each stage runs under its own name, and each call passes a `cpu_budget_ms`. A run that spends more CPU than its budget stops, naming the stage. The budgets are CPU time, never wall time, because wall time on this two-CPU box doubles when something else is building, and the CPU a program spends barely moves. So a busy box can't turn the verdict red, and a stage gone slow can. A real run:

{{fence repos:essay-surface/journal/wednesday.md "5 programs, 2.4 s in all"}}

### A check before shipping

`full.shale` runs `fast.shale` first, then five stages on all of Steve's real notes:

{{lines repos:essay-surface/scripts/full.shale from "local load = essay_load()" to "SAFE TO SHIP"}}

Every stage runs, even after one fails, so the last line names every stage that failed. The number in each call is how many of the stage's last lines to show.

The bench needed more care, since its whole job is time. The server now logs, as it stops, how many requests it served and the CPU they took, so the bench holds CPU a request to a budget:

{{lines repos:essay-surface/scripts/bench.shale from "The most CPU a request may take" to "const CPU_BUDGET_US"}}

The first version held one figure for the whole mix, and it hid things: 1,615 posts dominated it, and a return to the old scan would have added only about 125 µs to the average. So each phase got a server of its own. To prove the index budget, the agent put the old scan back once: 2,170 µs a request, against a budget of 400. Wall-clock medians are still reported, and judged only on a quiet box:

{{lines repos:essay-surface/scripts/bench.shale from "local quiet =" to "latency: not judged"}}

Slowness is a finding too. The anchoring simulation took 13 s, so the agent profiled it: the time was the work itself, 187,000 simulated edits, and it stays.

### The test that hid others

The store's new tests all passed on their first run, and a green is a claim. So the agent put in a fault on purpose, a deaf watch, one line that made the queue read nothing, and ran the tests. Three went red. Every test that edits a note should have, so the agent ran the plant again, with the panic lines shown:

{{fence repos:essay-surface/journal/wednesday.md "slice bounds out of range [:-1] [recovered, repanicked]"}}

Tuesday's `TestCommentsFollowEdits` sliced `page[:i]` with `i` at -1 when the page had no orphans section. The panic ended the test binary, and every test after it went unreported. Tuesday's run gave no sign, because on Tuesday nothing failed. With the panic made a plain failure, the same deaf watch turned twelve tests red. Chapter 5 makes that trick a habit.

Then the full check went red on purpose: the renderer wrote `<b>` for `<strong>`, which a browser hides. The verdict was right, `NOT SAFE: fast, race failed`, but the red fast stage showed its last lines of `go test -v`, which were passes. Now fast keeps only the failed tests and their lines, and its stop names them:

{{fence repos:essay-surface/journal/wednesday.md "1 failed: TestRender (86718 steps)"}}

The audit passed that plant. It looks for markdown left over, never for which tags appear, and the renderer's tests caught it. Each check has its own job.

### Now a gate

`fast.shale` runs on every edit, in about three seconds, and stops at the first stage that fails with the names of the tests that failed. `full.shale` runs before anything ships, holds every stage to a CPU budget, and ends on one line: `SAFE TO SHIP`, or `NOT SAFE` and the stages that failed.
