# Automating agentic development with Shale

A book for experienced developers who are new to working with agents. We follow one side project, a new essay surface built in a week of chapters, and read the Shale our agent wrote along the way.

This is draft 2, rough in places: prose and code, with pictures still to come.

## Introduction

Shale is a programming language written by agents, for agents. It looks like Lua on purpose, and it mostly works like any mainstream language: variables, functions, loops, lists and maps, strings and files. Its unique features are built for its niche, which is scripts an agent writes and a human reads.

That niche is the premise of this book. You will read a lot of Shale and write very little. Your agent writes it, every time it searches your code, runs your tests, renames across a tree or ships a release. You read it as it scrolls by, judge it, and steer the agent that wrote it. Here we learn to do that well.

### Who this book is for

This book is for experienced developers: people who have written plenty of code, but not much of it with an agent. You know a build from a test and a test from a gate. You may have tried an agent on a small task and wondered what it would take to trust one with a real project. You're curious about programming languages, and open to letting an agent do real work.

You won't need to install anything to follow along. Every script in the book really ran, and the book shows what it printed.

### The project

The book follows one side project from start to finish. We build a new essay surface: a small web app where Steve writes notes in markdown, reads them in a browser, and replies to any paragraph. A Go server sits behind a plain-JavaScript front end. By the end of the week Steve was using it for real, and he wrote his replies to the drafts of this book on it.

We tell it as a week, a day per chapter, because that's how the work fell into stages. It's a side project, not five calendar days: the real build took an afternoon of agent time. An essay surface isn't an agent harness, though the two have something in common. In both, a human reads what was written, replies, and steers.

### We, and the prompts

The book says "we" for Steve and his agent. Each day opens with the prompt we gave the agent, quoted whole. Then it follows the work through the agent's own Shale, quoted from the journal the agent kept as it went. The agents were real, the prompts were real, and so were the mistakes. When something went wrong, the book says so, along with what caught it and what habit it taught.

### What Shale adds

Most of what you'll read is ordinary code. A few features are Shale's own:
- **Every run has budgets:** steps, reads and output. A runaway script stops and says which budget it hit.
- **A script works inside its own folder** and the named folders it mounts, so you can see everything it can reach.
- **A risky program runs when the script gives its reason.** A shell, an interpreter or a text tool needs a `want = "tag: why"`, and the reason is logged.
- **A stop is one line** that says where and why.
- **A prelude does the daily work:** git, jobs, HTTP, JSON, and refactors that read code the way its compiler does.

Shale builds nothing itself. `go build` makes the server. Shale runs the build, keeps what it said, and judges what came out.

### Gates

The thread through the whole week is gates. A gate is a script that holds work to a standard. It runs the checks, prints a verdict on its last line, and fails when the work falls short. Gates are how we write quality software with agents, because they turn a rule into something that runs on every change.

In this project, nobody planned the gates up front. They arrived when the work asked for them: a check before a commit on Monday, a fast check and a full one by Wednesday, faults planted on purpose on Thursday, a release with a rollback on Friday.

### A language that changes

Shale is meant to have dialects. A dialect costs a team of humans a great deal, because everyone has to learn it. It costs an agent very little, because an agent reads the whole script, and the docs for its own dialect. So you can keep the dialect that works for your agent, and build your own library of scripts on top of the cookbook or instead of it.

The week shows this happening. A nuisance came up four times, so on Saturday the language changed to fix it. For now, a reader who wants that freedom forks their own copy of Shale.

Monday starts with an empty repository and a prompt.

## Monday: a server and a number

Our week starts with an empty repository and this prompt:

{{quote repos:essay-surface/journal/prompts/monday.md}}

Two phrases shape the day. "Speed matters to me" asks for a number, and "five hundred notes" says where to measure it. Steve has about five hundred real notes, so the agent works against those.

### The first stop

The agent began by reading Shale's own docs, and its first script stopped:

{{fence repos:essay-surface/journal/monday.md "Before you reach"}}

{{fence repos:essay-surface/journal/monday.md "index 0 is outside the list"}}

A stop is one line: the place (line 7, column 28), a class (`index`), and the reason. At scroll speed, ask three questions of any script. What does it read? What does it run? How did it end? This one reads a doc, runs no programs, and ended at a stop. The cause sits on its first line: `lines` splits a string, so `ls` held one line, the path itself. A file's lines are `read_lines`. The agent took the hint and read the doc with its file reader, the right tool for prose.

### Budgets

To pick a dozen varied samples, the agent surveyed every note: its size, and how many fences, tables and list items it holds. The first try stopped on a budget:

{{fence repos:essay-surface/journal/monday.md "the step budget (5000000 steps) ran out"}}

Every Shale run has budgets for steps, reads, bytes and memory. `re_lines` is written in Shale itself, so over 4.7 MB of notes it spends a step a character, and the budget stops the run long before it runs away. The agent switched to `grep`, a builtin, and hit the memory budget next. The docs give the fix: do each file's work in a function, and call `yield()` after each file, so the run holds one file at a time.

{{fence repos:essay-surface/journal/monday.md "wt = wt + 1"}}

This reads like Lua, on purpose. `function` declares a function above where it's called, and `end` closes it. `{path = ..., bytes = ...}` is a map, `#` is a length, and `..` joins strings. `glob` lists files, `stat` gives a file's size, and `sort` and `table` turn a list of maps into a printed table. It reads the notes, runs no programs, and ended well:

{{fence repos:essay-surface/journal/monday.md "total bytes 4747688"}}

Five hundred was the real count, with a long tail: the biggest note is 167 KB. Smaller stops came the same way all day, a guessed field name here and a guessed builtin there, each named in one line and fixed in the next call.

### A gate before a commit

The agent wrote the server and its renderer in Go, standard library only. Shale builds nothing itself: `go build` makes the server, and Shale runs the build, keeps what it said, and judges what came out. The first test run had one red, a table rule the agent chose to keep and wrote down as a test of its own. Then it saved its first recipe, `scripts/check.shale`: gofmt must list nothing, vet must be quiet, and every test must pass. Its first run stopped, which is its job:

{{fence repos:essay-surface/journal/monday.md "fail: gofmt (131 steps)"}}

Once the gate passed, the agent counted what ran, since a green is a claim:

{{fence repos:essay-surface/journal/monday.md "how many tests and subtests actually ran?"}}

`run` takes a program as a list of words, with a timeout and a keep: here three minutes, and the whole output up to 200 KB. `must` turns a failed program into a failed script. Every program a script runs goes through `run` or a call like it, so a program is easy to spot at scroll speed.

{{fence repos:essay-surface/journal/monday.md "PASS lines: 69"}}

### The number

`job_start` runs a program that outlives one call, and `job_wait` watches its log for a line. The agent started the server and fetched two pages:

{{fence repos:essay-surface/journal/monday.md "start the server on the sample notes"}}

{{fence repos:essay-surface/journal/monday.md "index 200, 3742 bytes, 9 ms"}}

Nine milliseconds was the wrong number. `http_get` starts a curl process per call, so it timed curl starting up. The agent saved `scripts/bench.shale`, which runs one curl over many URLs on a kept-alive connection and reads curl's own clock. On all 503 real notes:

{{fence repos:essay-surface/journal/monday.md "in 178ms"}}

A note takes 0.13 ms. The index takes 2 ms, so the agent split it by phase with two Go benchmarks, and found 1.1 ms of it was a `stat` of every note on every request. That went first on the list for tomorrow.

### The audit

The tests checked the cases the agent thought of. To find the rest, it rendered every real note through the server and counted markdown left over outside code. The first try stopped:

{{fence repos:essay-surface/journal/monday.md "budget runs: 64 programs a run"}}

Each `http_get` is a program, and a run gets 64. Worse, the stop came after `job_start`, so the server stayed up. A job's record is its log's name plus `.job`:

{{fence repos:essay-surface/journal/monday.md "the job's record is <log>.job"}}

So the saved `scripts/audit.shale` fetches every page with one curl, stops the server, and only then lets `must` judge the fetch. A stop in between can't leave the server running. It cuts code out of each page with builtins, which cost a step a call:

{{function repos:essay-surface/scripts/audit.shale cut}}

It ran in under a second and found two real bugs:

{{fence repos:essay-surface/journal/monday.md "<title>Review: gopher-roc"}}

Titles kept their markdown in 35 notes, and a lone triple backtick in a table cell swallowed the rest of its row. Each fix got a test, the table one copied from the note that found it. The audit of 503 real notes found two bugs that 62 hand-written renderer tests missed. The agent's own verdict in its report: write the audit before the tests next time. Tests check the cases you thought of, and real data checks the rest.

### What's a check now

`check.shale` runs before every commit: gofmt, vet and every test. The audit's count is the next gate, once something proves the audit itself is watching.

## Tuesday: replies that stay put

Tuesday's prompt asks for the feature the surface exists for:

{{quote repos:essay-surface/journal/prompts/tuesday.md}}

The first bullet is the hard one. The old surface stores a comment's place as a position, `"para_index": 4`, so a paragraph added above sends every comment below it one paragraph off. Today we read the agent's scripts at review speed, too. It proposes to keep some of them, and the question for each is whether it earns its place.

### Arguing from real paragraphs

The obvious design: a comment remembers its paragraph's text, and on each render it goes to the most similar block above some threshold. The agent measured that before building it. A Go test scored every block of every real note against its nearest other block, and against itself after a typo, a changed word or a split:

{{fence repos:essay-surface/journal/tuesday.md "words: nearest other block"}}

Scores are in thousandths. One typo in a short block drops it to 250, while 1% of blocks have a look-alike in the same note at 714 or more, so the two ranges overlap. A simulation of the decision itself made that concrete:

{{fence repos:essay-surface/journal/tuesday.md "trigrams deleted  T=300"}}

At any threshold low enough to keep an edited short block, a deleted block drags its comments onto a look-alike. So the design added structure. A comment also remembers the fingerprints of the blocks before and after it, and goes, in order, to the block with the same text, the block still between the same two neighbors, the most similar block, or the orphans at the foot of the page. A deleted block leaves its neighbors touching, so the second rule tells a deletion from an edit.

The first simulation of the real resolver ran into its timeout, and the stop came on the line that read its exit code:

{{fence repos:essay-surface/journal/tuesday.md "110.0 s in all"}}

A killed program has no `code`; its `ended` says why. The agent treated the slowness as a finding, cached each block's features once a note, and ran it again:

{{fence repos:essay-surface/journal/tuesday.md "precompute features once per note"}}

At scroll speed: it edits one test file, runs `go test` once with `env` pointing it at the real notes, and prints the lines for one threshold.

{{fence repos:essay-surface/journal/tuesday.md "real notes, ended exit, 49158 ms"}}

Across 22,900 real paragraphs, every edit in place kept its comments. The agent chose the word measure at 600 because it misplaced fewer deleted paragraphs' comments, 345 against 635: a wrong placement misleads, and an orphan is honest. That's the day's turn. The design was argued from Steve's own paragraphs, and the comparison is committed as it ran, so anyone can run it again.

### Seeding faults

Sixteen hand-written anchoring tests passed on their first run. A green is a claim, so the agent broke the code twice, on purpose, and put it back each time:

{{fence repos:essay-surface/journal/tuesday.md "plant 1: no in-place rule"}}

We call this a plant; the established name is fault seeding. `edit(path, old, new)` replaces text that must occur exactly once, so each break and each repair lands exactly where it says. Each `run` goes without `must`, since a red is the point. The output names the tests each fault reached:

{{fence repos:essay-surface/journal/tuesday.md "--- FAIL: TestAnchorTypoFixed"}}

At review speed, this script runs once and goes. It proved the tests once, and a fault left in place would prove nothing more. The agent did the same for the front end: `check.shale` now runs `node --check comments.js`, since `go test` never parses it, and one planted syntax error turned that stage red.

### A script worth keeping

`scripts/comments_smoke.shale` runs the whole comment flow through the real server. It posts a comment, fixes a typo and adds a paragraph above it on disk while the server runs, then deletes the paragraph. Its checks go through one small function:

{{function repos:essay-surface/scripts/comments_smoke.shale want}}

The end of the script shows how it holds a server:

{{lines repos:essay-surface/scripts/comments_smoke.shale from "local job = essay_start(" to "for l in result.value do print(l) end"}}

`attempt` runs `flow` and gives back its result or its stop, so the server is stopped before any verdict. That is the review question for every script that starts a server, and `bench.shale` failed it once today. A budget stop is the one stop `attempt` can't catch, and the server stayed up until the agent stopped it by its record. The bench's budget header now leaves room.

{{fence repos:essay-surface/journal/tuesday.md "ok: and the file still has it"}}

### Every check passed

The report listed six things to check from now on. The last was an eye test the agent couldn't run, since the box has no browser. Steve opened a note in his, and found no way to reply. The buttons sat at opacity 0 and appeared only on hover. On the old surface, buttons follow only prose paragraphs, so hover had him hunting for buttons that weren't there. His words: "Hover is an anti-feature."

The fix set the buttons at opacity 0.6. Their first label was "note", which already meant the essays, so a second commit made it "reply". Steve has written his real replies on the new surface ever since. The first landed on a bullet about "a rename pass", and it read back correctly from the comments file. On Thursday, hidden reply buttons become a check that runs without a browser.

### What's a check now

`check.shale` parses the front end and pins the anchoring simulation on the samples, so any change to the anchoring shows its new numbers. The browser caught what every check missed, and that miss is the next check.

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

`opts.stage` names the stage, and each call passes a `cpu_budget_ms`. A run that spends more CPU than its budget stops, naming the stage. The budgets are CPU time, never wall time, because wall time on this two-CPU box doubles when something else is building, and the CPU a program spends barely moves. So a busy box can't turn the verdict red, and a stage gone slow can. A real run:

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

## Thursday: making the checks fail

{{quote repos:essay-surface/journal/prompts/thursday.md}}

Wednesday ended on `SAFE TO SHIP`. Could that line ever say anything else?

### One fault per stage

The technique is old. Putting a fault in once, to prove a check catches it, is fault seeding, or mutation testing when a machine does it. We call a seeded fault a plant. Steve had never heard the word before he worked with agents, and plants sometimes irritate him: they slow gates, often through needless recompiles. Done right, they catch tests that test nothing, as Wednesday's deaf watch did in chapter 4.

`full.shale` has six stages, and its first, `fast`, has five of its own, so Thursday had eleven plants. Every launch has the same shape:

{{fence repos:essay-surface/journal/thursday.md "plant 1, fast's gofmt stage"}}

The fault goes in, the stage runs exactly as `full.shale` runs it, and the reverse `edit` takes the fault out in the same launch. Then `git diff` must be empty and `fast.shale` must pass before the next plant. Each plant costs a rebuild and one run of its stage, from a few seconds to twenty, once. None was committed.

Every stage went red for its fault. The race detector caught a map read without its lock, and `fast` passed with that fault in. The audit named the four real notes an aligned-table fault broke. The dates stage named one real note that a plausible "show the latest edit" change re-dated. The bench caught a cache kept but unused, at 1,776 µs of CPU a request against a budget of 400. Three stages are the only ones that see their faults: race, bench and the smoke.

### A stop with the fault still in

Plant 6's first launch passed `run` an option without the one it needs, and Shale stopped the script after the fault went in and before the reverse edit. The fault sat in `store.go`. The agent took it out before anything else:

{{fence repos:essay-surface/journal/thursday.md "the last launch stopped after planting"}}

Plants 1 to 5 had been lucky. After that, every plant ran under `shale --undo-on-stop`, which puts back every path a stopped run changed. The next try stopped on the agent's own `print`, and the footer showed the undo doing its job:

{{fence repos:essay-surface/journal/thursday.md "undone: 1 path(s) the run changed put back"}}

### Green with the fault in

The fourth plant wrote a commenter's name to the page without escaping it. The tests checked the comment's text, and nothing checked the name:

{{fence repos:essay-surface/journal/thursday.md "ok  test, 2474 ms"}}

That is a real security hole. A commenter named `<script>...` would run code on every reader's page, and every stage of the full check passed with it. The missing piece was a test, and the agent wrote one that puts markup in every field the page shows:

{{fence repos:essay-surface/journal/thursday.md "func TestEveryCommentFieldEscaped"}}

It passed before the plant, failed with it, and passed after. The smoke's plant found a smaller flaw: its red said a file was missing, and now it names the check that failed.

### Fixtures, and what they cost

Steve asked for fixtures that are cheap: data the check reads, never a build of its own. Three kinds stayed. The audit reads a known-bad page and a known-good one before any real note, in about 2,000 steps and under 5 ms. A plant that blinded the audit stopped it there, before any server started. Wednesday's hidden panic became data for `fast.shale`'s reader of `go test`:

{{lines repos:essay-surface/scripts/fast.shale from "A known-bad go test -v output" to "misreads testdata"}}

That costs 3 ms and 3,184 steps, and a plant that stopped the reader from recognizing a panic tripped it at once. The third kind is the reply-button sheets, below.

The rest proved their stages once and left. A known-bad Go file would turn `gofmt` and `vet` red forever unless excluded, and an exclusion is a blanket. A racy program needs a `-race` build of its own, exactly the cost Steve named. A slow bench fixture needs a server.

### Reply buttons, without a browser

On Tuesday every check passed, and Steve opened a page with no visible way to reply: the buttons sat at `opacity:0` until hovered. `buttons_test.go` works the cascade out by hand, on the CSS the page is served with. It judges each place `comments.js` puts a button, at every width a media query names, light and dark. A button is hidden by `display:none`, `visibility:hidden`, or opacities that multiply out to under 0.5. CSS it can't judge, such as `!important`, stops the test. Its header names its limits: it never runs the script, and it doesn't see clipping or contrast.

Its fixtures are seven small sheets, five known-bad and two known-good, at 0.02 s inside `go test`. One is Tuesday's real sheet, taken from git:

{{fence repos:essay-surface/journal/thursday.md "the known-bad and known-good sheets for the reply-button check"}}

Putting Tuesday's `opacity:0` back turned the test red at 51 and 80rem. The checker got a plant of its own, opacities no longer multiplied, and `faint-article.css` caught it:

{{fence repos:essay-surface/journal/thursday.md "faint-article.css: want"}}

### Steve's two replies

Two replies to Wednesday's report arrived mid-run. The first asked for a defensive check that the comments folder's parent exists. The server already failed there, but by accident. Now it checks first, names the path, and makes nothing:

{{fence repos:essay-surface/journal/thursday.md "made: false"}}

The second said phones are out of scope, and to state that clearly. The README now says so in one sentence, and the by-hand list keeps only the desktop browser. The narrow-window CSS stays: out of scope means unchecked, not removed.

`full.shale` went from 30.9 s to 23.3 s, mostly from a quieter box; Thursday's additions cost tens of milliseconds.

### Now a gate

Every stage of `full.shale` has been seen red once, and its kept fixtures run on every check in milliseconds. A page that hides its reply buttons fails `fast.shale` now, and so does a commenter's name that reaches the page unescaped.

## Friday: shipping should be boring

{{quote repos:essay-surface/journal/prompts/friday.md}}

By Friday the surface was in daily use, with real replies on it, and every release was a chore done by hand. A chore done by hand is done a little differently each time. A release done by a script is the same every time, and that sameness is what we mean by boring.

### A release, in order

The agent's script, `release.shale`, does what the prompt asks, in order. It stops before it touches anything unless the checkout is clean and exactly one server runs with the argv it would start. Then it runs the full check from Thursday and reads its last line:

{{lines repos:essay-surface/scripts/release.shale from "-- 2: the full check." to "full check: SAFE TO SHIP"}}

Both have to hold: the check exits 0, and it says SAFE TO SHIP. Next comes the build. Shale builds nothing itself: `go build` makes the binary, beside the old one, and the script writes down the commit it came from and how long the build took.

Before it stops anything, the script copies the comments into a dated folder and compares the copy file by file. It also fetches the old server's index and every page with comments on it, as a record of where each comment sat. Then it stops the old server, keeps its binary as `.prev`, starts the new one, and checks it. The comment check is strict: each comment must follow the same block of the note as before, and still be in its file.

The swap and the checks run under `attempt`, which catches a failure and hands it back as a value:

{{lines repos:essay-surface/scripts/release.shale from "local done = attempt(rel_install_and_check, t, before)" to "ROLLED BACK: running"}}

On a failure, the old binary goes back, starts, and gets the same checks the new one failed. A failed release ends where it started, and says so on its last line.

### A check that talked to the wrong server

The first surprise came before the script ran at all. The trial put an "old" server on the dev port, 9300, and the checks' own servers listened there too. The agent ran the smoke check beside the trial server to see what would happen:

{{fence repos:essay-surface/journal/friday.md "the page: 404"}}

The smoke's server logged "listening", then failed to bind, and the smoke went on to ask the trial server for its page. Every script waited for "listening", and the server said it before the port was its own. Here the check failed. With a different server on the port, a check could have passed against the wrong program. Two fixes followed: `server.go` now binds first and says "listening" after, and the checks moved to port 9310, named once in the prelude.

### The trap, sprung

Monday's journal warned that a run that stops after `job_start` leaves its server running. Friday's first trial hit it:

{{fence repos:essay-surface/journal/friday.md "the step budget (5000000 steps) ran out"}}

`attempt` catches a failure, and a budget stop is past its reach. The run stopped after the old server was down and the new one was up, so the rollback never ran. The trial was left serving a build nobody had checked. On the live server, that would have been a release with no check and no way back but by hand, which is how the agent put the trial back.

The cause was a regex over a 67 KB page, at a step a character; plain `split` and `find` now read the pages. The defense was a step budget twenty times what a release uses, with the reason written above it. The trap itself stayed in the language until Saturday.

### A release that fails on purpose

The prompt asked for a release that fails, and the agent chose its fault carefully. A fault the full check catches never reaches the swap, so it proves nothing about the rollback. The agent looked for a class of fault no check covers, and found one: the server's defaults. Every check passes `-comments` explicitly. The live server passes none. So the agent seeded one fault, on a throwaway branch in its own worktree, that moves the default comments folder inside the notes folder:

{{fence repos:essay-surface/journal/friday.md "*comments = filepath.Join(filepath.Clean(*dir)"}}

The full check said SAFE TO SHIP. The release caught it on the first comment and rolled back:

{{fence repos:essay-surface/journal/friday.md "FAILED: comment f31eaaa22556"}}

Afterward every comments file matched the data byte for byte, and the bad binary was kept as `.failed`, with its log. That's the case for a release check. The full check tests the code the way the tests start it. The release check tests the server the way it really runs, with its real flags and its real data, and only the second one sees a bad default.

A fourth trial seeded a formatting fault, and the full check stopped it: `nothing touched`, the same server pids before and after. Then the agent removed the plant's branch and worktree, since a seeded fault proves a check once.

### The real release

When it worked on dev, we ran it ourselves, on the live server:

```text
release: /home/steve/essay-dogfood on 127.0.0.1:9301
running: pid 2484343, unknown (built before release.shale)
   == fast: PASS, 3558 ms, cpu 3457 ms
   == race: PASS, 2740 ms, cpu 1677 ms
   == anchoring: PASS, 12281 ms, cpu 11753 ms
   == audit: PASS, 1350 ms, cpu 602 ms
   == dates: PASS, 1698 ms, cpu 351 ms
   == bench: PASS, 4925 ms, cpu 1636 ms
full check: SAFE TO SHIP, 26562 ms
built bin/essay-surface.new: 92fb48b built 2026-11-13T17:51:07Z: Journal: Friday, the final script released once more on dev, 926 ms
comments kept: 3 files, 7 comments, in essay-dogfood/releases/2026-11-13T175107Z/comments, each compared
the old server's index and 3 commented notes kept in essay-dogfood/releases/2026-11-13T175107Z/before
stopped the old server; its log kept
started 92fb48b built 2026-11-13T17:51:07Z: Journal: Friday, the final script released once more on dev
ok: the index, 508 notes, the same as before
ok: a note, 2026-11-01-a-day-of-judges.md, 18882 bytes
ok: day-2026-11-02.md, 200, its 1 comments where they were
ok: essay-surface-wednesday.md, 200, its 5 comments where they were
ok: shale-book-draft-2-plan.md, 200, its 1 comments where they were
ok: all 7 comments from before on their pages and in their files
RELEASED: running 92fb48b on 127.0.0.1:9301; to roll back to unknown: cd /home/steve && shale repos:essay-surface/scripts/release.shale data=essay-dogfood port=9301 rollback=1
```

Read it the way you'd read any gate: the verdicts. Six stages passed, the build took under a second, seven comments were kept and found where they were, and the last line says what runs and how to go back. The old version reads `unknown` because it was built by hand, before the script wrote versions down. The full check is nearly all of the 27 seconds; the swap and its checks fit inside one.

The release was the most boring thing we did all week, and it took the most care to make it so. Boring is what a release looks like when every surprise was found on the dev port first.

## Saturday: the language gets better

Four times this week, an agent's run stopped after `job_start` and left a server running: on Monday, on Tuesday, on Thursday during a planted fault, and in Friday's first trial, on a budget stop. Each time, someone noticed and cleaned up by hand.

Four times is a pattern, and a pattern of hand cleanup is a gap in the language. So on Saturday we closed it, in Shale itself.

### The rule

The rule fits in one sentence: a run that stops ends the jobs it started. A `fail`, an error or a budget, anything but the script's own end, stops each job that run started and that still runs. A run that ends normally leaves its jobs running, because a server started on purpose should stay up. A failure that `attempt` catches is no stop, since the run goes on.

Some jobs must outlive even a stop, so `job_start` gained one option, `keep_on_stop`. Here is the prelude's `job_start` as it stands now, with the comment that tells an agent about both:

{{function repos:shale/src/shale_prelude_linux.shale job_start}}

When a run stops, its report says what happened to each job, in one line apiece. These two are from a live run that stopped with two jobs up, one of them kept:

```text
── stopped with the run, 1 job it started: `sleep 31` (log scratch:keep-test.7WQsc0/plain.log)
── left running (keep_on_stop): `sleep 30` (log scratch:keep-test.7WQsc0/kept.log)
```

That's the same shape as every stop in this book: one line, what happened, and where to look. A reader scanning a stopped run sees at once which servers are gone and which are still serving.

### How it arrived

A cold agent built it. Shale's interpreter is Zig, so the feature is Zig too: the host keeps each job a run starts, and the runner ends the running ones when the run stops. The tests cover every side of the rule. A run that ends keeps its job. A failed run ends both of its jobs and names them. A budget stop ends its job. A caught failure ends nothing. A `keep_on_stop` job survives the stop and is named apart. A job the run already stopped or waited for gets no second stop. A job another run started is never touched. Two faults seeded into the feature turned those tests red, so the tests are watching.

Then the feature passed Shale's own tests and the consumers gate, which runs the gates of every repository that uses Shale. A change to what a stop means touches every script with a job in it, so that gate is the one that matters. It passed, and we merged it.

### The code says what it keeps

The feature changed what a stop does, and one script had to say something new. A failed release ends with `fail`, which is a stop. It ends that way on purpose, after it has put the old server back and checked it. Under the new rule, that stop would end the old server too. So `release.shale` gained one word:

{{function repos:essay-surface/scripts/release.shale rel_start}}

The new server starts with `keep` false, and the old one put back by a failed release starts with it true. Before Saturday, the script's defense against the trap was a large step budget and a comment explaining it. Now the code says which server stays, and the language does the rest.

It also changes Friday's trap. If a budget stop comes after the swap, the unchecked new server now ends with the run, and the stop line names it. The run still has no way to put the old server back, so nothing serves until someone runs the rollback. That's a loud failure in place of a quiet one: an unchecked server stays up only while its checks run.

One limit remains. A `shale` killed from outside, by Ctrl-C or by a timeout around it, never reaches its own stop, so its jobs run on. That's why the release's header asks for a long timeout on the call that runs it.

### Dialects

This is what we mean by a language that agents make better. The feature came from the week's own work. The work needed it four times, the nuisance had a clear shape, and an agent turned it into a rule, its tests and a one-line report.

`keep_on_stop` is the first word of this project's dialect of Shale. A dialect costs a human a great deal: new words to learn, old habits that no longer fit. It costs an agent very little, since the agent reads the docs every session. So Shale is meant to have dialects. Each reader keeps the one that works for their agent, and builds their own scripts on top of it.

For now, that means forking your own copy of Shale and letting your agent change it. The habit matters more than the mechanism. When a cleanup happens by hand a second time, ask your agent what word the language is missing. Then read the tests on both sides, see the seeded faults turn them red, and run the gates of everything that depends on it.

The best feature of the week came from its most annoying chore. Your agent's nuisances are your language's roadmap.

## How we work

A week of work leaves habits behind. Here they are in one place, since they're the real subject of the book.

### Three ways to read

Most of the Shale you'll meet goes by quickly. Read it at three speeds:
- **At scroll speed,** a script going by: what does it read, what does it run, and how did it end? A Shale script names every program it runs, and its stop is one line, so a few seconds is enough.
- **At review speed,** a script the agent wants to keep: is it worth having again? Read its code, not its opening comment. Check its arguments, the way it fails, and its last line, which is its claim about what it did.
- **At trust speed,** a gate: what does it hold, how does its count stay honest, and what proves it's watching? A gate you've never seen fail is a gate you haven't tested.

### A check for each moment

By Friday the project had a check for every moment, each sized to how often it runs:

| When | The check | Its time |
|---|---|---|
| each edit | `fast.shale`: format, vet, the tests, a smoke test | about 3 s of CPU |
| before shipping | `full.shale`: fast, the race detector, the real notes | about 25 s |
| shipping | `release.shale`: full, then a checked swap with a rollback | about 27 s |

A check that's too slow stops getting run, so each one is held to a budget, and the budget is CPU time, which a busy machine can't break. Slowness is a finding.

### Prove it can fail

Every gate on the list earned its place by failing once on purpose. On Wednesday a planted fault found a test whose panic hid twelve others. On Thursday one found a security hole that every stage had passed. Seeding faults costs a few minutes, once. Keep as permanent fixtures only the cheap ones: data a check reads, never something that needs a build of its own.

### Look with your own eyes

Every check passed on Tuesday, and the reply buttons were invisible. Gates hold what you already know to check. A human's first look finds what you didn't know yet, and then the agent can often turn it into a gate.

### Let the work ask

Nothing this week was planned in advance. The checks, the release script and the language change all arrived because a chore came up more than once. That's how to grow a project's tools: make a script the third time you do something by hand, a gate once the script makes a judgment, and a change to the language once its gap starts to cost you.

### Your dialect

That last step is the one most languages don't offer. Shale changes as its agents work, and yours can too. Fork it, keep the dialect your agent works best in, and build your own library of recipes. The scripts in this book are a starting point. The ones your agent writes for your own work will be better.
