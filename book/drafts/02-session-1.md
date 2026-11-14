# Draft

## Session 1: the bugs the tests missed

Session 1 starts with an empty repository and this prompt:

{{quote repos:essay-surface/journal/prompts/session-1.md}}

The orchestrator wrote this prompt from a plan Steve gave it and handed it to a cold agent. The agent worked for about twelve minutes, and Steve didn't step in. "Five hundred notes" says where to work: Steve has about that many on the old surface, the Python server he used before this one, and the agent works against the real ones.

The agent wrote the server and its markdown renderer in Go, with no dependencies, and a dozen real notes to serve as samples. What this chapter follows is how it checked that work, because that's where Shale comes in.

### A gate before a commit

Before its first commit, the agent saved a recipe, `scripts/check.shale`. Every commit after it passes through this script, so it's the first one worth reading closely. Here it is as Session 1 left it:

{{show repos:essay-surface d793c5a:scripts/check.shale}}

Read it the way you'd read any short script. Three programs run, one after another: `gofmt -l` lists any file that isn't formatted, `go vet` looks for suspicious code, and `go test` runs the tests. Each `run` starts a program and keeps what it said. `must` fails the script unless the program exited 0, and `fail` stops it with a reason. So the script either prints three verdicts or stops on the first one that's wrong.

When it passed, it said so in three lines:

{{fence repos:essay-surface/journal/session-1.md "vet: clean, 454 ms"}}

### A green is a claim

A passing gate says the tests ran. It doesn't say how many did, or whether the ones you meant to add were among them. So the agent counted:

{{fence repos:essay-surface/journal/session-1.md "how many tests and subtests actually ran?"}}

{{fence repos:essay-surface/journal/session-1.md "PASS lines: 69"}}

Sixty-nine passed, seven of them top-level tests, and none failed. That's a number you can check against the test file, which "ok" never is. Count what ran, every time a gate turns green for the first time. Green ✅

### The audit

The tests checked the cases the agent thought of. To find the rest, it saved a second recipe, `scripts/audit.shale`. It starts the server on all of Steve's real notes, fetches every page, stops the server, and then counts markdown left over outside code: a stray `**`, a `](`, a table's `|---`, or a backtick. Any of those in a rendered page is a construct the renderer missed.

It found two real bugs that the 62 hand-written test cases had missed:

{{fence repos:essay-surface/journal/session-1.md "<title>Review: gopher-roc"}}

Titles kept their markdown, and a lone triple backtick in a table cell swallowed the rest of its row. Each fix got a test, the table one copied from the note that found it.

Run your code over real data before you trust your own tests. Your tests hold only the cases you thought of, and five hundred real notes hold the rest. The audit cost one script and under a second of running, and it found what the tests couldn't.

Not every script ran the first time. The agent hit a few stops along the way, each one line, and fixed each in its next call. Reading a stop is a craft of its own, and it can wait until you know what a run that works looks like.

The agent also saved a benchmark. A note takes about a tenth of a millisecond to serve, which is all a first version needs to know.

### The gates now

`check.shale` runs before every commit: gofmt, vet and every test. The audit runs by hand for now, and Session 3 makes it a stage of a gate.
