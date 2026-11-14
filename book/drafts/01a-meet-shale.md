# Draft

## Meet Shale

The quickest way to meet a language is to read some, so here's a real gate: `scripts/check_before_commit.shale`. It's from later in the project: the agent runs it on every edit. First, the helper that runs each stage:

{{function repos:essay-surface/scripts/check_before_commit.shale stage}}

Then the stages, in order: the formatter, `go vet`, a syntax check of the front end, the Go tests, and a smoke test through the real server.

{{lines repos:essay-surface/scripts/check_before_commit.shale from "local fmt = stage(" to "checks through the real server"}}

If you've read Lua, this looks familiar, and that's on purpose. `local` declares a variable, `function` and `end` frame a function, braces build a list or a map, and `..` joins strings. The rest is the mainstream you know.

Agents write Shale tersely, so you'll meet names like `ls` for a list of lines. Steve scans for the calls, such as `lines`, and skips the variables: "The honest truth is that I skim shale scripts really fast, because I have seen a lot of it." If you can't skim it that fast yet, ask your agent to write more verbosely. It will: terseness is just a habit agents fall into when nobody objects.

An agent usually hands a script to `shale` from bash, as a heredoc: everything between `<<'EOF'` and the closing `EOF` goes to `shale` as its input. Here's a real one:

```bash Shale, launched from bash
cd ~/showell_repos/essay-surface && shale <<'EOF'
local r = must(run({"go", "vet", "."}, {timeout_ms = 60000, tail = 20}))
print("vet: clean, " .. r.ms .. " ms")
EOF
```

It printed `vet: clean, 386 ms`.

### What Shale adds

A few features are Shale's own:
- **Every run has budgets:** steps, reads, bytes, memory and programs started. A runaway script stops, naming the budget it hit.
- **A script works inside its own folder** and the folders it mounts. A mount is a named folder beyond its own, written `name:path`, so you can see everything a script can reach.
- **A risky program runs when the script gives its reason.** A shell, an interpreter, a text tool or a program git doesn't track needs one, and Shale logs it.
- **A prelude does the daily work.** The prelude is Shale's built-in library: git, jobs, HTTP, JSON, and refactors that read code the way its compiler does. A job is a program that outlives the call that started it, such as a server.

### Who builds what

Shale builds programs by orchestrating other tools. In a Go project, the agent uses Shale to run Go's own tools, `gofmt`, `go vet`, `go build` and `go test`, and to check what each one said.

### Who reads the output

Shale's output is written for the agent. A run that stops ends on one `── stopped at` line, naming where and why, and a `── used:` line for its budgets. The agent reads them and tells Steve what he needs. Agents translate output well, so Shale leaves that to them. This book shows a raw stop line with its meaning beside it.

### The cookbook

Shale ships with a cookbook: a folder of ready-made scripts, `cookbook/` in the Shale repository, that agents wrote. Each script in it is a recipe, general enough for any project, and a recipe comes in two kinds. Most you run directly, with their arguments on the launch line: `shale cookbook:disk.shale floor=60` stops when the disk has under 60 GB free. A few are libraries: a script loads one with `use("cookbook:conflicts.shale")`, then calls its functions, which parse git's merge-conflict markers. Recipes get you running.

A script lives in a project's `scripts/` folder instead, and encodes that project's standards, such as what every commit must pass. Your scripts are the value you add to your own ecosystem. When a second project needs one, it becomes a recipe in your own cookbook, beside Shale's or in place of it.

### How an agent learns Shale

Agents are linguists. Learning a language from its docs is what they do best, even a language newer than their training: Steve has watched lesser models than today's give themselves crash courses in recent Roc and Zig. Steve's setup points each agent to Shale's README, which points it to an agent guide. In one test, a fresh agent told only that its shell work is Shale read both, listed the cookbook, and passed its project's commit check, all in about 40 seconds.

### A language that changes

Shale is meant to have dialects. A dialect costs a team of humans a great deal and an agent very little, since it reads the docs every session. Session 6 shows a new word arriving, and what that means for your own copy.

### What it costs

Shale doesn't save tokens, and it may add to them. It also slows you at first, while your agent builds its scripts. What it buys is a higher-level language than bash, where you can build real primitives. A bash script strings commands together and greps their text. In Shale, `run` returns a record with the exit code and the output, and a helper like check_before_commit.shale's `stage` becomes a word the next script uses. Each primitive makes the next script shorter and easier to read.

### Four words for what's next

Four words come up from the first session on. `run` needs a timeout and a keep, how much of a program's output it holds on to:
- **`want`:** the reason a script gives for running a risky program, written `want = "tag: why"`.
- **`whole`:** a keep in bytes: all of the output up to that size.
- **`tail`:** a keep in lines: the program's last lines, where its verdict usually sits.
- **`must`:** `must(r)` returns a run that exited 0, and fails the script on any other, printing the output and naming the program.

That's enough Shale to start. Next, one turn of the loop, from a reply to a change.
