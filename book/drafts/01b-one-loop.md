# Draft

## One loop

Before the first session, here's one complete turn of the loop the whole book is made of: an agent reports, Steve replies, the agent changes the code, and we read what it ran. It comes from Session 4, and it took a few minutes.

```dot
digraph loop {
  rankdir=LR;
  node [shape=box, style="rounded", fontname="Helvetica", margin="0.15,0.08"];
  edge [fontname="Helvetica"];
  steve [label="Steve"];
  agent [label="The agent"];
  gates [label="Gates\nfast, full, release"];
  surface [label="The essay surface"];
  steve -> agent [label="a prompt"];
  agent -> gates [label="runs, in Shale"];
  gates -> agent [label="a verdict", style=dashed];
  agent -> surface [label="a report"];
  surface -> steve [label="he reads"];
  steve -> surface [label="replies"];
  surface -> agent [label="the replies", style=dashed];
}
```

*The loop. Steve steers with prompts and replies, and the agent answers with code that has passed its gates, plus a report.*

### The report

Jump ahead to the end of Session 3. The agent's report there listed a few decisions Steve might reverse. One said the server needs the comments folder's parent to exist when it starts. Steve read the report on the essay surface and replied to that paragraph. The server appended his reply to the note's comments file, one JSON line per reply:

{{fence repos:essay-surface/journal/reply.md "add a defensive check"}}

That's the whole of the steering. Attach each reply to the paragraph it answers: the paragraph carries the context, so four words are a complete instruction. The `quote` is that paragraph, and `prev` and `next` fingerprint its neighbors, as Session 2 explains. The orchestrator passed this reply and one more to the agent already at work on Session 4, mid-run.

### The change

The agent added the check in Go, where the server starts:

{{lines repos:essay-surface/store.go from "// The parent is checked, never made" to "is not a folder; make it"}}

Its comment says why: a mistyped folder name would otherwise grow a folder of comments where nobody looks. It added a test for a missing parent, a parent that's a file, and one that exists. Then it planted a fault once, saw the test go red, and took the fault out.

### What we read

Last, the agent ran the real server once with a folder that doesn't exist, to see the message where a person would meet it. This is what scrolled by:

{{fence repos:essay-surface/journal/thursday.md "what the real server says when the comments folder"}}

{{fence repos:essay-surface/journal/thursday.md "made: false"}}

Read every script that goes by at scroll speed, with three questions. A few seconds a script is what lets you judge an agent's work as fast as it runs:
- **What does it read?** One folder of notes, and whether `.run/no-such-folder` exists afterwards.
- **What does it run?** `./essay-surface`, the server it just built. Because that binary isn't tracked by git, `run` requires a reason, and the script gives one: `want = "build: the server we just built"`.
- **How did it end?** Exit 1, with the message, and `made: false`: the server stopped and created nothing.

That run shows the check working for real, and the test and the planted fault cover the rest.

### Why Shale, and not bash

The same check in bash is one line: run the server, echo `$?`, list the folder. It works on a good day. On a bad day, when the check is missing, the server starts listening and never exits, and the agent's shell hangs on it. The Shale version stops waiting: `run` requires a timeout, here 10 seconds, and a keep, here the last 5 lines. A program the repository doesn't track runs only with a reason, and the reason is logged. Every line of the script says what it touches, so the reader can judge it at a glance. That's the bargain the whole book rests on: slightly longer scripts that a human can read in seconds.
