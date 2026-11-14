# Draft

## Session 1: the bugs the tests missed

Session 1 starts with a new repository that holds only a README, and this prompt:

{{quote repos:essay-surface/journal/prompts/session-1.md}}

The orchestrator wrote this prompt from a plan Steve gave it and handed it to a cold agent, and Steve didn't step in. "Even with five hundred notes" is a speed target, and it's also a real count: the old surface, the Python server Steve used before this one, holds 502 notes. The agent copied a dozen of them in as samples, and measured and audited against all of them.

The agent wrote the server and its markdown renderer in Go, with no dependencies. The prompt also says to commit as you go, so the agent commits on its own, with nobody watching, and each commit needs a bar to clear.

### A gate before a commit

Before its first commit, the agent saved `scripts/check.shale`, and it runs that script before every commit:

{{show repos:essay-surface d793c5a:scripts/check.shale from "local fmt" to "test.ms"}}

Read it with One loop's three questions. What does it read? Nothing: it only runs programs. What does it run? Three of them: `gofmt -l`, which lists any file that isn't formatted, `go vet`, which looks for suspicious code, and `go test`. How does it end? With a verdict for each, or a stop. Only `go vet` goes through `must`. `gofmt -l` exits 0 even when it lists files, so the script checks its output instead, with `~=` (not equal). A failed `go test` prints what it said before `fail` stops the script. `.ms` is how long a run took.

The gate's first real run listed two files that needed formatting:

{{fence repos:essay-surface/journal/session-1.md "gofmt: these files need gofmt -w:" from "gofmt: these files" to "markdown_test.go"}}

The agent ran `gofmt -w` on both, and the next run passed:

{{fence repos:essay-surface/journal/session-1.md "vet: clean, 454 ms"}}

Green ✅

Before you let an agent commit on its own, give it a gate that every commit must pass. Nobody reads an unattended commit as it lands, so the gate is the review it gets.

### A green is a claim

Then the agent counted what ran. A one-off script reruns the tests with `-v` and counts the PASS and FAIL lines:

{{fence repos:essay-surface/journal/session-1.md "how many tests and subtests actually ran?"}}

{{fence repos:essay-surface/journal/session-1.md "PASS lines: 69"}}

69 is the seven top-level tests plus the 62 cases inside `TestRender`: every test in the files, and none failed. Count what ran whenever a gate first turns green, and again whenever you add tests. A count can be checked against the test file, and "ok" can't.

### The easy part

Next the agent saved `scripts/bench.shale`, which times the server through one curl. A note took about 0.13 ms and the index about 2 ms, inside the prompt's few milliseconds. Speed was the easy part. Correctness wasn't.

### The audit

The renderer's tests are examples the agent chose, and the notes weren't written to fit them. Steve's agents write them, as essays he replies to, with whatever markdown comes to hand. A survey of the dozen samples alone found raw HTML, fences of four tildes, and lists nested six spaces deep. The notes themselves are the only full list of what they hold, so the agent checked every one.

Then it saved `scripts/audit.shale`, which starts the server on all the real notes, fetches every page, and counts markdown left over outside code: a stray `**`, a `](`, a table's `|---`, or a backtick. Inside code those characters are legitimate. Outside it, each is worth a look, and it's usually a construct the renderer missed.

The audit stops the server before it judges anything, so nothing that fails afterward can leave a server running. It fetches, stops, and only then checks the fetch:

{{show repos:essay-surface d793c5a:scripts/audit.shale from "local fetched = run(argv" to "must(fetched)"}}

The order came from a mistake. The audit's first try fetched each page as a program of its own, and Shale stopped the script at its cap on programs a run, after the server had started and before anything stopped it. The agent found the server still running, stopped it, and wrote the saved script so that can't happen again. It was one of about ten stops in the session, and most were smaller.

The first full run flagged 37 pages. Most of them came from two real bugs: titles kept their markdown, in 35 notes, and a lone triple backtick in a table cell opened a code span that never closed, so it swallowed the rest of its row, pipes and all:

{{fence repos:essay-surface/journal/session-1.md "<title>Review: gopher-roc"}}

Each fix got a test, the table one copied from the note that found it. After the fixes, two pages were flagged, and both were faithful output: CommonMark renders their sources the same way.

Run your code over real data before you trust your own tests. Your tests hold only the cases you thought of, and five hundred real notes hold many you didn't. When real data finds a bug, copy the real case into a test. The agent's own hindsight goes one step further. Asked what it would do differently tomorrow, it said it would write the audit before the renderer's tests.

### The gates now

`check.shale` runs before every commit: gofmt, vet and every test. The audit runs by hand for now, and Session 3 makes it a stage of a gate.
