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
