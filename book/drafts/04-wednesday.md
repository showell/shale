# Draft

## Wednesday: a check for each moment

{{quote repos:essay-surface/journal/prompts/wednesday.md}}

By Wednesday the essay surface had four scripts, and Tuesday's report listed what to run and when. A list is something to remember. Today it became two commands.

### Measure, then listen

Monday's numbers had already named the cost: the index checked every note's file on every request. Before touching it, the agent measured the parts of its plan. One survey counted where the 505 real notes keep their dates: nearly always on line 3, in the paragraph after the title. Another timed `git log` on the notes at 0.8 s, slowed by 3,961 unpacked objects. That repository is Steve's, so the agent left it alone, and the server pays the cost once, at start-up.

The new server hears changes from the kernel, through Linux's inotify, and reads the kernel's queue of events at the start of each request. The kernel queues an event before the write returns, so the next request always sees it. The index page is kept until a note changes. Before, the index took fifteen times as long as a note:

{{fence repos:essay-surface/journal/wednesday.md "median 2.276 ms" from "index:" to "notes, no comments"}}

After, it took 0.1 ms, as fast as a note:

{{fence repos:essay-surface/journal/wednesday.md "median 0.105 ms, p90 0.143 ms" from "index:" to "notes, no comments"}}

A note's date now comes from its first lines, or else its last commit. A fresh clone, which resets every file's time, gives the same index, all 506 notes in place. That comparison became a stage of the full check.

The mistakes on the way stopped loudly. The prelude's `edit_between`, which replaces the text between two anchors, moved the store out of `server.go` and reported `-175 +3 lines`. That count showed it had cut the imports too, before anyone looked. A builtin the agent guessed at, `take_last`, stopped its script before a line ran.

### A check for each edit

`fast.shale` is the check Steve runs without thinking. Its body is five stages: the formatter, `go vet`, the front end's syntax, every Go test, and a smoke test through the real server:

{{lines repos:essay-surface/scripts/fast.shale from "local fmt = stage" to "checks through the real server"}}

The last two stages count what ran: "44 tests passed, 63 subtests" can be checked, and "ok" can't.

Each stage goes through one function, whose stop names the stage and its failed tests:

{{function repos:essay-surface/scripts/fast.shale stage}}

A run that spends more CPU than its `cpu_budget_ms` stops, naming the stage. The budgets are CPU time, never wall time: wall time on this two-CPU box doubles when something else is building, and the CPU a program spends barely moves. So a busy box can't turn the verdict red, and a stage gone slow can. A real run takes 2.4 s:

{{fence repos:essay-surface/journal/wednesday.md "5 programs, 2.4 s in all"}}

### A check before shipping

`full.shale` runs `fast.shale` first, then five stages on all of Steve's real notes: the Go tests under the race detector, Tuesday's anchoring simulation (which replays edits to see where comments land), Monday's audit of every rendered note, the dates comparison, and the bench. Every stage runs, even after one fails, so the last line names every stage that failed:

{{lines repos:essay-surface/scripts/full.shale from "local failed = {}" to "SAFE TO SHIP"}}

The bench needed more care, since its whole job is time. The server now logs, as it stops, how many requests it served and the CPU they took, so the bench holds each request's CPU to a budget:

{{lines repos:essay-surface/scripts/bench.shale from "The most CPU a request may take" to "const CPU_BUDGET_US"}}

The first version held one budget for the whole mix of requests, and the bench's 1,615 comment posts outweighed the rest: a return to the old scan would have added only about 125 µs to the average. So each phase got a server of its own. To prove the index budget, the agent put the old scan back once, and it measured 2,170 µs a request against the budget of 400. Wall-clock medians are still reported, and judged only when the box is quiet.

Slowness is a finding too. The anchoring simulation took 13 s, so the agent profiled it: the time was the work itself, 187,000 simulated edits, and it stays.

### The test that hid others

The store's new tests all passed on their first run. So the agent seeded a fault, a deaf watch: one line that made the server read nothing from the kernel's queue. Three tests went red, but every test that edits a note should have. The agent ran it again with the panic lines shown:

{{fence repos:essay-surface/journal/wednesday.md "slice bounds out of range [:-1] [recovered, repanicked]" from "slice bounds" to "0.042s"}}

Tuesday's `TestCommentsFollowEdits` sliced `page[:i]` with `i` at -1 when the page had no orphans section, where a deleted paragraph's comments go. The panic ended the test binary, and every test after it went unreported. Tuesday's run gave no sign, because on Tuesday nothing failed. With the panic made a plain failure, the same deaf watch turned twelve tests red. Thursday makes that trick a habit.

Then the full check went red on purpose: the renderer wrote `<b>` for `<strong>`, which looks the same on the page. The verdict was right, `NOT SAFE: fast, race failed`, but the red fast stage showed the last lines of `go test -v`, and those were passes. Now fast keeps only the failed tests and their lines, and its stop names them:

{{fence repos:essay-surface/journal/wednesday.md "1 failed: TestRender (86718 steps)" from "markdown_test.go:100" to "stopped at"}}

Monday's audit, which reads every rendered note for markdown left over, passed that fault, since it never checks which tags appear; the renderer's tests caught it. Each check has its own job.

```dot
digraph checks {
  rankdir=LR;
  node [shape=box, style="rounded", fontname="Helvetica", margin="0.15,0.08"];
  edge [fontname="Helvetica"];
  edit [label="each edit", shape=plaintext];
  ship [label="before shipping", shape=plaintext];
  shipping [label="shipping", shape=plaintext];
  fast [label="fast.shale\nformat, vet, tests, smoke\nabout 3 s of CPU"];
  full [label="full.shale\nfast, race, the real notes\nabout 25 s"];
  rel [label="release.shale\nfull, then a checked swap\nabout 27 s"];
  edit -> fast;
  ship -> full;
  shipping -> rel;
  fast -> full [label="runs inside", style=dashed];
  full -> rel [label="runs inside", style=dashed];
}
```

*A check for each moment. Each runs the one before it, so a release has passed them all.*

### Now a gate

`fast.shale` runs on every edit, in about three seconds, and stops at the first failing stage, naming the failed tests. `full.shale` runs before anything ships, holds every stage to a CPU budget, and ends on one line: `SAFE TO SHIP`, or `NOT SAFE` and the stages that failed.
