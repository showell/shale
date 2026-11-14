# Draft

## Meet Shale

Before the story starts, a short tour. The quickest way to meet a language is to read some, so here's a real gate from the project: `scripts/fast.shale`, the check the agent runs after every edit. It takes a few seconds. First, the helper that runs each stage:

{{function repos:essay-surface/scripts/fast.shale stage}}

Then the stages, in order. One more line after them prints the verdict, `fast: PASS`.

{{lines repos:essay-surface/scripts/fast.shale from "local fmt = stage(" to "checks through the real server"}}

If you've read Lua, this looks familiar, and that's on purpose. `local` declares a variable, `function` and `end` frame a function, braces build a list or a map, and `..` joins strings. The rest works like any mainstream language: variables, functions, loops, lists and maps, strings and files. You can read most of it the first time through.

### What Shale adds

A few features are Shale's own, and most of them show up in fast.shale:
- **Every run has budgets:** steps, reads and output. A runaway script stops and says which budget it hit. fast.shale adds one of its own to each stage, `cpu_budget_ms`, so a stage that turns slow fails.
- **A script works inside its own folder** and the named folders it mounts, so you can see everything it can reach. Every path in fast.shale, `.` or `comments.js`, is inside the repository.
- **A risky program runs when the script gives its reason.** A shell, an interpreter or a text tool needs a `want = "tag: why"`, and the reason is logged. `node` is an interpreter, so the js stage says why it runs: to check the front end's syntax, which go test can't see.
- **A stop is one line** that says where and why. When a stage fails, fast.shale ends on one line that names it, such as `vet: go exited 1`.
- **A prelude does the daily work:** git, jobs, HTTP, JSON, and refactors that read code the way its compiler does. `run` comes from the prelude, and so do the HTTP calls in the smoke script, the last stage.

### Who builds what

Shale builds nothing itself. `go build` makes the server, and `go test` compiles the tests. Shale runs those programs, keeps what they said, and judges what came out: fast.shale reads go test's output and counts the tests that passed.

### A language that changes

Shale is meant to have dialects. A dialect costs a team of humans a great deal, since everyone must learn it, and an agent very little, since it reads the whole script and its dialect's docs. So keep the dialect that works for your agent, and build your own library on Shale's cookbook of scripts or instead of it.

The language changes on purpose, and your agent can make it better. In Session 6, a nuisance comes up three times, and the language changes to fix it. For now, a reader who wants that freedom forks their own copy of Shale.

### Four words for what's next

`run` needs a timeout and a keep: how much of a program's output to hold on to. These four words come up from the first session on:
- **`want`:** the reason a script gives for running a risky program, written `"tag: why"`. Shale logs it.
- **`whole`:** a keep in bytes. `run` holds all of the output up to that size, and each stage in fast.shale holds up to 400,000 bytes.
- **`tail`:** a keep in lines. `run` holds the program's last lines, where its verdict usually sits.
- **`must`:** `must(r)` passes a run that exited 0 through, and fails the script on any other, printing the output and naming the program. fast.shale's `stage` does the same job, with more to say when a test fails.

That's enough Shale to start. Next, one turn of the loop, from a reply to a change.
