# Draft

## Session 1: the bugs the tests missed

Session 1 starts with an empty repository and this prompt:

{{quote repos:essay-surface/journal/prompts/session-1.md}}

The orchestrator wrote this prompt from a plan Steve gave it and handed it to a cold agent. The agent worked for about twelve minutes, and Steve didn't step in. "Five hundred notes" says where to work: Steve has about that many on the old surface, the Python server he used before this one, and the agent works against the real ones.

The agent's journal records 52 blocks of scripts and output. This chapter shows five, each as it ran.

### Seven stops in twelve minutes

Before writing any code, the agent read Shale's own docs. It wanted one table from them, the one that says which builtin to use in place of each shell habit, so its first script looked for the line where that table starts:

{{fence repos:essay-surface/journal/session-1.md "Before you reach"}}

It stopped:

{{fence repos:essay-surface/journal/session-1.md "index 0 is outside the list"}}

You don't need to read a line like that, because the agent does. It names the place and the reason. Here `lines` had split the path's text, not the file, so nothing matched, and the agent read the doc with its file reader in its next call. The session had seven stops, and this is the whole record of them:

| The stop | What the agent did next |
|---|---|
| index 0 is outside the list | `lines` splits a string, not a file, so it read the file instead |
| the step budget ran out | a regex in a loop over 500 notes; it switched to `grep`, a builtin |
| the arena budget ran out | the loop held every note's work at once; now it holds one at a time |
| no key "mtime" in the map | a guessed field name; the real one is `modified` |
| fail: gofmt | its own new gate, doing its job |
| no function `first` | a guessed builtin |
| 64 programs a run | one request per note; the audit below tells this one |

That's what an agent's first session with a language looks like, and it's cheap. Each stop is one line, and none cost more than the next call. An agent guesses (a field name, a builtin), and the stops correct its guesses before they can do anything.

### A green, counted

The agent wrote the server and its markdown renderer in Go, with no dependencies. Then it saved its first recipe, `scripts/check.shale`: gofmt must list nothing, vet must be quiet, and every test must pass. Once the gate passed, the agent counted what ran:

{{fence repos:essay-surface/journal/session-1.md "how many tests and subtests actually ran?"}}

Sixty-nine passed, and none failed. A green is a claim: a passing gate says the tests ran, and only a count shows that they did. Green ✅

The agent saved a benchmark too. A note takes about a tenth of a millisecond to serve, which is all a first version needs to know.

### The audit

The tests checked the cases the agent thought of. To find the rest, it rendered every real note through the server and counted markdown left over outside code. The first try fetched each page as a program of its own, and stopped:

{{fence repos:essay-surface/journal/session-1.md "budget runs: 64 programs a run"}}

This is the one stop with stakes. It came after the server had started, so it left a server running. The agent found the server by the record Shale keeps of each job, and stopped it. Then it built the lesson into the saved script, `scripts/audit.shale`: one curl fetches every page, the server stops, and only then is anything judged. A script that starts a server must stop it before anything can stop the script.

The audit found two real bugs that the 62 hand-written test cases had missed:

{{fence repos:essay-surface/journal/session-1.md "<title>Review: gopher-roc"}}

Titles kept their markdown, and a lone triple backtick in a table cell swallowed the rest of its row. Each fix got a test, the table one copied from the note that found it. Run your code over real data before you trust your own tests: your tests hold only the cases you thought of, and five hundred real notes hold the rest.

### The gates now

`check.shale` runs before every commit: gofmt, vet and every test. The audit runs by hand for now, and Session 3 makes it a stage of a gate.
