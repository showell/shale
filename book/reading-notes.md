# Steve's reading notes, queued

Steve is reading the whole book cover to cover (from 2026-11-14) and sends bits of feedback in the console. Each one lands here as he said it, under its chapter, and waits for a pass. Nothing here is applied piecemeal. The introduction is on hold (MISSION, Holds), so its notes wait for that hold to lift.

## Introduction

- (Steve, 2026-11-14) It's starting to pile up inaccuracies as the book's scope grows. It's structurally fine; fix once the dust settles.

## Meet Shale

- (Steve, 2026-11-14) State plainly that agents write Shale in a terse style, so the reader will see variable names like `ls` for a list of lines. Steve has gotten used to it: he scans for the calls, such as `lines`, and doesn't really read the variables. "The honest truth is that I skim shale scripts really fast, because I have seen a lot of it." Say this plainly, with no apology and no call to fix the style. It fits One loop's reading speeds too.
- (Steve, 2026-11-14) He almost never reads Shale's raw output, such as a `── stopped at the Linux prelude:223:15 ... budget steps ...` line with its `── used:` line. The agent reads it and turns it into something human-readable. Emphasize those mechanics: Shale's output is written for the agent, and the agent translates it into what Steve needs. "Shale's job is to automate agent task and not to short-circuit what agents are perfectly good at, which is translating shale's output into what I need to consume." This touches every chapter that shows a stop or a `── used:` line. The book should say once, early, who reads that output, and later chapters can show the agent's translation beside the raw lines. A candidate MISSION rule, AL, when applied.

## Session 1

- (Steve, 2026-11-14) "I didn't realize that the go server was using markdown early in the narrative. That slipped by me earlier." Say plainly that the agent wrote its own markdown renderer in Go (`markdown.go`, about 1,200 lines, no dependencies), so Part Two's choice reads against it.

## Part Two (Meet Roc, Sessions 8 and 9)

- (Steve, 2026-11-14) Part Two must acknowledge that a perfectly good markdown renderer already exists, and that building a new format is an intentional decision: "It's totally defensible, to be clear, but we need to state why clearly." The facts: markdown notes keep rendering through `markdown.go` unchanged, and `.note` is a second format beside it (MISSION PB). The reasons to state: markdown holds diagrams and widgets badly; a format designed from what Steve actually writes is small (Session 8's counts); and porting takes the fun out (Steve). FOR THE EDITOR'S PASS NOW, not later: Part Two is in review.

## Session 2 (for its rewrite)

The Session 1 rewrite (MISSION AN) no longer sets these up, and Session 2's draft leans on them, from the flow reviewer, 2026-11-14:
- reading a stop line, including a killed run's stop, which has no `code`, only `ended`;
- budgets as stops: "stopped on its budget", and a budget stop as the one stop `attempt` can't catch;
- `yield`, and a script's `-- budget:` header line;
- job records ("stopped it by its job record"), which first came up when Session 1's audit left a server running;
- `essay_start` and `essay_stop`, since Session 1's audit used `job_start` and `job_stop` directly;
- the run record's fields (`code`, `out`, `ms`), which Meet Shale covers only loosely.
Session 1 now names `bench.shale`, and states that the audit stops the server before it judges anything, which Session 2's `attempt` passage builds on. Rewrite Session 2 from its sources (MISSION AL), and decide where in the ladder each of these is first taught.

## Later: themes and transparency, not yet actionable (Steve, 2026-11-14)

- **How the book was really built.** The first half of the book was almost truly a one-shot. When we get back to rewriting the introduction (still frozen), clarify that framing, and be slightly more transparent about how the book was actually built.
- **Maintaining Shale is cheap for Steve, and a positive story.** When an agent finds a problem in Shale and fixes it (Session 1's `lines`, the regex built-ins), that shows how little Steve has to do to maintain the language. Skipping such a story early isn't portraying something hard as easy: "It was easy for Steve. I would argue that it wasn't even hard for the agent."
- **A theme for the book to cover eventually:** "Agents are really good at fixing compilers for the same reason they are good at writing compilers!" A ready example: Steve asked for color syntax for Shale in one shot, and the agent built it, on Shale's own lexer, nearly at once. "It was right in your wheelhouse. It looks great!"

## Steve's answers (the question series, 2026-11-14)

- **Heredocs:** as a reader, he wouldn't have known what a heredoc is before agents. Gloss it once, early.
- **The orchestrator in Session 1:** keep it. The reader should know from the start who wrote the prompt.
- **Why only `go vet` uses `must`:** noise. Say what the script checks, not how each check is coded.
- **Go code in the book:** a little, a short excerpt now and then, so the reader sees what's being checked.
- **"Cold agent":** define it once, briefly, where it first appears. "The concept is easy, the term is ours, though, not universally known."
- **"Recipe" and "cookbook":** define them once, cookbook first, without belaboring either. The metaphor was invented by the agents, and Steve likes it, but it's unique to Shale. "Cookbook seems more common to me. Recipes aren't completely new to some people."
- **Shale's shipped cookbook:** introduce it in Meet Shale, as one of Shale's selling points.
- **Recipe versus script (Steve's lean, then agreed):** two words, because they do different work. A recipe lives in a cookbook (Shale's shipped one, or your own): general, works in any project, gets you up and running. A script lives in a project's `scripts/`: specific, encodes that project's standards, and is the value the reader adds to their own ecosystem. So the book says the agent saved a *script* (`check.shale`), not "its first recipe", and the word changes throughout.
- **Promotion:** name it, briefly and plainly at first: a script that a second project needs becomes a recipe in your own cookbook. The deeper lesson about how code evolves is real, but too nuanced to introduce early.
- **Would a reader know what to ask for, after Session 1?** Yes, mostly. "An experienced developer knows that he is responsible for holding the standards for gates at the highest possible level." But Steve doesn't ask for scripts or recipes by name: Claude finds them for him, and he occasionally reminds Claude to keep everything well organized for future agents. The book should show that: the human holds the standard, the agent finds and organizes the scripts. (The question series ended here, at Steve's call, 2026-11-14.)

- (Steve, 2026-11-14) "When an agent looks sloppy" in How we work (drift, overreach, drive past a limit; a correction lasts once it lives in a file or a check) is fundamental, and we may have to come back to it and clarify it for the reader. Some of it may improve in later generations of agents, and some are intrinsic problems agents share with humans.

- (Steve, 2026-11-14, MISSION AU) The epiphany from three diagrams: the book covered the advanced workflow (an orchestrator keeping cold agents busy) before the simple one, Steve asks, Claude builds and reports, Steve replies on a line, Claude fixes. Applied to One loop, Session 1 and How we work. Still to apply when Sessions 2 to 9 are rewritten: tell the work as "the agent", drop orchestrator and cold-agent detail up front (Part Two's "short read" and "the orchestrator drives" framing included), and teach orchestration late as an advanced technique, orthogonal to Shale, if at all.
