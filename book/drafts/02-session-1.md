# Draft

## Session 1: a server and a number

Our first session starts with an empty repository and this prompt:

{{quote repos:essay-surface/journal/prompts/monday.md}}

The prompt has the roles turned around: in practice the agent writes the notes, and Steve replies to them. Two phrases shape the session. "Speed matters to me" asks for a number, and "five hundred notes" says where to measure it. Steve has about that many on the old surface, so the agent works against those.

### The first stop

The agent began by reading Shale's own docs, and its first script stopped:

{{fence repos:essay-surface/journal/monday.md "Before you reach"}}

{{fence repos:essay-surface/journal/monday.md "index 0 is outside the list"}}

A stop is one line: the place (line 7, column 28), a class (`index`), and the reason. At scroll speed, ask three questions of any script. What does it read? What does it run? How did it end? This one reads a doc, runs no programs, and ended at a stop. The cause is on its first line. `lines` splits a string, not a file, so `ls` held one line: the path itself. A file's lines are `read_lines`. The agent read the doc with its file reader instead, the right tool for prose.

### Budgets

To pick a dozen varied samples, the agent surveyed every real note, in the old surface's folder `essay-repl-server/notes`: its size, and how many fences, tables and list items it holds. The first try stopped on a budget:

{{fence repos:essay-surface/journal/monday.md "the step budget (5000000 steps) ran out"}}

Every Shale run has budgets for steps, reads, bytes and memory. `re_lines` is written in Shale itself, so it spends a step a character, and over 4.7 MB of notes the budget stopped the run long before it could run away. The agent switched to `grep`, a builtin, and hit the memory budget next. The docs give the fix: do each file's work in a function, and call `yield()` after each file, so the run holds one file at a time.

{{fence repos:essay-surface/journal/monday.md "wt = wt + 1" from "function measure(p)" to "print(#all"}}

This reads like Lua, on purpose. `function` declares a function, and `end` closes it. `{path = ..., bytes = ...}` is a map, `#` is a length, and `..` joins strings. `glob` lists files, and `stat` gives a file's size. The script reads the notes, runs no programs, and ended well:

{{fence repos:essay-surface/journal/monday.md "total bytes 4747688" from "502 notes" to "167408"}}

Five hundred was the real count, with a long tail: the biggest note is 167 KB. Smaller stops came throughout, a guessed field name or builtin, each named in one line and fixed in the next call.

### A gate before a commit

The agent wrote the server and its renderer in Go. The first test run had one red, a table rule the agent chose to keep and wrote down as a test of its own. Then it saved its first recipe, `scripts/check.shale`: gofmt must list nothing, vet must be quiet, and every test must pass. Its first run stopped, which is its job:

{{fence repos:essay-surface/journal/monday.md "fail: gofmt (131 steps)"}}

Once the gate passed, the agent counted what ran, since a green is a claim:

{{fence repos:essay-surface/journal/monday.md "how many tests and subtests actually ran?"}}

`run` takes a program as a list of words, with a timeout and how much output to keep: here three minutes, and up to 200 KB. `must` turns a failed program into a failed script. Every program goes through `run` or a call like it, so programs are easy to spot at scroll speed. Seven top-level tests ran 69 cases, and none failed:

{{fence repos:essay-surface/journal/monday.md "PASS lines: 69"}}

Green again ✅

### The number

`job_start` runs a program that outlives one call, and `job_wait` watches its log for a line. The agent started the server and fetched the index. Its `want` gives the reason Shale asks for before running a program git doesn't track, like a server just built:

{{fence repos:essay-surface/journal/monday.md "start the server on the sample notes" from "job_start(" to "idx.ms"}}

The index came back in nine milliseconds:

{{fence repos:essay-surface/journal/monday.md "index 200, 3742 bytes, 9 ms" from "index 200" to "note 200"}}

That was the wrong number. `http_get` starts a curl process per call, so it timed curl starting up. The agent saved `scripts/bench.shale`, which runs one curl over many URLs on a kept-alive connection and reads curl's own clock. On all 503 real notes, a note takes about a tenth of a millisecond, and the index twenty times that:

{{fence repos:essay-surface/journal/monday.md "in 178ms" from "index: 200 requests" to "notes: 503 requests"}}

The agent split the index's 2 ms by phase with two Go benchmarks, and found 1.1 ms of it was a `stat` of every note on every request. It went first on the next session's list.

### The audit

The tests checked the cases the agent thought of. To find the rest, it rendered every real note through the server and counted markdown left over outside code. The first try stopped:

{{fence repos:essay-surface/journal/monday.md "budget runs: 64 programs a run"}}

Each `http_get` is a program, and a run gets 64. Worse, the stop came after `job_start`, so the server stayed up. Shale keeps a record of each job, a file named for its log plus `.job`, and the agent stopped the server by it:

{{fence repos:essay-surface/journal/monday.md "the job's record is <log>.job"}}

So the saved `scripts/audit.shale` fetches every page with one curl, stops the server, and only then lets `must` judge the fetch. It cuts code out of each page with builtins, which cost a step a call:

{{function repos:essay-surface/scripts/audit.shale cut}}

It ran in under a second and found two real bugs:

{{fence repos:essay-surface/journal/monday.md "<title>Review: gopher-roc"}}

Titles kept their markdown in 35 notes, and a lone triple backtick in a table cell swallowed the rest of its row. Each fix got a test, the table one copied from the note that found it. The audit of 503 real notes found two bugs that 62 hand-written renderer tests missed. The agent's verdict: write the audit before the tests next time.

### What's a check now

`check.shale` runs before every commit: gofmt, vet and every test. The audit's count is the next gate, once something proves the audit itself is watching.
