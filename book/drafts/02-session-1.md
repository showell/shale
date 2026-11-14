# Draft

## Session 1: the bugs the tests missed

Session 1 starts with a new repository that holds only a README, and this prompt:

{{quote repos:essay-surface/journal/prompts/session-1.md}}

The orchestrator wrote this prompt from Steve's plan and handed it to a cold agent, a new agent that starts with only its prompt and none of our conversation. Steve didn't step in.

The agent wrote the server first. It's a Go program, about 250 lines, using only Go's standard library: it reads the notes folder, turns each markdown file into an HTML page, and serves them, with an index. The markdown renderer is Go too, about a thousand lines, and the agent wrote it from scratch. Then it copied in a dozen of Steve's notes as samples and wrote the renderer's tests.

### A gate before a commit

The prompt says "commit as you go", and never asks for checks before a commit. The agent added them anyway, because checking code before you commit it is standard practice in Go, and an agent brings a language's standard practices with it, the way an experienced hire would.

Go ships the tools for those checks. `gofmt` is Go's official formatter: Go has one accepted layout for code, and `gofmt -l` lists every file that doesn't match it. `go vet` is Go's built-in checker for code that compiles but is probably wrong, such as a `Printf` whose arguments don't match its format string. `go test` runs the tests.

The agent saved the three checks as `scripts/check.shale`, and it runs that script before every commit:

{{show repos:essay-surface d793c5a:scripts/check.shale from "local fmt" to "test.ms"}}

Read it with One loop's three questions. What does it read? Only its programs' output. What does it run? Those three programs, one after another, to check that every file is formatted, vet is clean, and every test passes. How does it end? With a verdict for each, or a stop.

The gate's first real run listed two files that needed formatting:

{{fence repos:essay-surface/journal/session-1.md "gofmt: these files need gofmt -w:" from "gofmt: these files" to "markdown_test.go"}}

The agent ran `gofmt -w` on both, and the next run passed:

{{fence repos:essay-surface/journal/session-1.md "vet: clean, 454 ms"}}

Green ✅

Before you let an agent commit on its own, give it a gate that every commit must pass. Nobody reads an unattended commit as it lands, so the gate is the review it gets.

Steve holds that standard, and he never asks for a script by name. The agent finds the scripts a project has, writes the ones it lacks, and keeps them organized for the next agent, with a reminder from him now and then.

### A green is a claim

Then the agent counted what ran. A one-off script reruns the tests with `-v` and counts the PASS and FAIL lines:

{{fence repos:essay-surface/journal/session-1.md "how many tests and subtests actually ran?"}}

{{fence repos:essay-surface/journal/session-1.md "PASS lines: 69"}}

69 is the seven top-level tests plus the 62 cases inside `TestRender`: every test in the files, and none failed. Count what ran whenever a gate first turns green, and again whenever you add tests. A count can be checked against the test file, and "ok" can't.

### The easy part

Next the agent saved `scripts/measure_server.shale`, which times the server through one curl. A note took about 0.13 ms and the index about 2 ms, inside the prompt's few milliseconds. Speed was the easy part. Correctness wasn't.

### The audit

The renderer's tests are examples the agent chose, and the notes weren't written to fit them. Steve's agents write them, as essays he replies to, with whatever markdown comes to hand. A survey of the dozen samples alone found raw HTML, fences of four tildes, and lists nested six spaces deep. The notes themselves are the only full list of what they hold, so the agent checked every one.

Then it saved `scripts/check_rendered_notes.shale`, which starts the server on all the real notes, fetches every page, and counts markdown left over outside code: a stray `**`, a `](`, a table's `|---`, or a backtick. Outside code, each is usually a construct the renderer missed.

The audit stops the server before it judges anything, so a failure afterward finds the server already stopped. It fetches, stops, and only then checks the fetch:

{{show repos:essay-surface d793c5a:scripts/audit.shale from "local fetched = run(argv" to "must(fetched)"}}

The order came from a mistake. The audit's first try fetched each page as a program of its own, and Shale stopped the script at its cap on programs a run, after the server had started and before anything stopped it. The agent found the server still running, stopped it, and wrote the saved script so that can't happen again. It was one of about ten stops in the session, and most were smaller.

The first full run flagged 37 pages. Most of them came from two real bugs: titles kept their markdown, in 35 notes, and a lone triple backtick in a table cell opened a code span that never closed, so it swallowed the rest of its row, pipes and all:

{{fence repos:essay-surface/journal/session-1.md "<title>Review: gopher-roc"}}

Each fix got a test, the table one copied from the note that found it. The title fix is one call, `TitleHTML`, where the server makes each note's title and page:

{{show repos:essay-surface d793c5a:server.go from "n.title = TitleHTML(Title(text))" to "n.page = notePage(n, Render(text))"}}

After the fixes, two pages were flagged, and both were faithful output: CommonMark renders their sources the same way.

Run your code over real data before you trust your own tests. Your tests hold only the cases you thought of, and Steve's real notes hold many you didn't. When real data finds a bug, copy the real case into a test. Asked what it would do differently, the agent said it would write the audit before the renderer's tests.

### The gates now

`check.shale` runs before every commit: gofmt, vet and every test. The audit runs by hand for now, and Session 3 makes it a stage of a gate.
