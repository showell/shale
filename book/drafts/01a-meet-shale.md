# Draft

## Meet Shale

The quickest way to meet a language is to read some, so here's a real gate: `scripts/fast.shale`. It comes from later in the project, where it's the check the agent runs on every edit, in a few seconds. First, the helper that runs each stage:

{{function repos:essay-surface/scripts/fast.shale stage}}

Then the stages, in order: the formatter, `go vet`, a syntax check of the front end, the Go tests, and a smoke test through the real server. One more line after them prints the verdict, `fast: PASS`.

{{lines repos:essay-surface/scripts/fast.shale from "local fmt = stage(" to "checks through the real server"}}

If you've read Lua, this looks familiar, and that's on purpose. `local` declares a variable, `function` and `end` frame a function, braces build a list or a map, and `..` joins strings. The rest is the mainstream you know.

### What Shale adds

A few features are Shale's own, and most of them show up in fast.shale:
- **Every run has budgets:** steps, reads, bytes read and printed, memory, and the programs it starts. A runaway script stops and says which budget it hit. A program can get a CPU budget too, and fast.shale gives each stage one, `cpu_budget_ms`, so a stage that turns slow fails.
- **A script works inside its own folder** and the folders it mounts. A mount is a named folder beyond its own, written `name:path`, so you can see everything a script can reach. Every path in fast.shale, such as `.` or `comments.js`, is inside its repository.
- **A risky program runs when the script gives its reason.** A shell, an interpreter, a text tool or a program git doesn't track requires a reason, and the reason is logged. `node` is an interpreter, so the js stage says why it runs.
- **A stop is one line** that says where and why. When a stage fails, fast.shale ends on one line that names it, such as `vet: go exited 1`.
- **A prelude does the daily work.** The prelude is Shale's built-in library: git, jobs, HTTP, JSON, and refactors that read code the way its compiler does. A job is a program that outlives the call that started it, such as a server. `run` comes from the prelude too.

### Who builds what

Shale builds nothing itself. `go build` makes the server, and `go test` compiles the tests. Shale runs those programs, keeps what they said, and checks what came out: fast.shale reads go test's output and counts the tests that passed.

### A language that changes

Shale is meant to have dialects. A dialect costs a team of humans a great deal, since everyone must learn it, and an agent very little, since it reads the docs every session. Session 6 shows a new word arriving, and what that means for your own copy.

The same goes for recipes, scripts saved to a file because their question came back. Shale has a cookbook of them, and your agent builds its own library on top of it, or instead of it.

### Four words for what's next

Four words come up from the first session on. `run` needs a timeout and a keep, how much of a program's output to hold on to, and two of the four are keeps:
- **`want`:** the reason a script gives for running a risky program, written `want = "tag: why"`. Shale logs it.
- **`whole`:** a keep in bytes. `run` holds all of the output up to that size, and each stage in fast.shale holds up to 400,000 bytes.
- **`tail`:** a keep in lines. `run` holds the program's last lines, where its verdict usually sits.
- **`must`:** `must(r)` returns a run that exited 0, and fails the script on any other, printing the output and naming the program. fast.shale's `stage` does the same job, with more to say when a test fails.

That's enough Shale to start. Next, one turn of the loop, from a reply to a change.
