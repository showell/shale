# Steve's reading notes, queued

Steve is reading the whole book cover to cover (from 2026-11-14) and sends bits of feedback in the console. Each one lands here as he said it, under its chapter, and waits for a pass. Nothing here is applied piecemeal. The introduction is on hold (MISSION, Holds), so its notes wait for that hold to lift.

## Introduction

- (Steve, 2026-11-14) It's starting to pile up inaccuracies as the book's scope grows. It's structurally fine; fix once the dust settles.

## Session 1

- (Steve, 2026-11-14) "I didn't realize that the go server was using markdown early in the narrative. That slipped by me earlier." Say plainly that the agent wrote its own markdown renderer in Go (`markdown.go`, about 1,200 lines, no dependencies), so Part Two's choice reads against it.

## Part Two (Meet Roc, Sessions 8 and 9)

- (Steve, 2026-11-14) Part Two must acknowledge that a perfectly good markdown renderer already exists, and that building a new format is an intentional decision: "It's totally defensible, to be clear, but we need to state why clearly." The facts: markdown notes keep rendering through `markdown.go` unchanged, and `.note` is a second format beside it (MISSION PB). The reasons to state: markdown holds diagrams and widgets badly; a format designed from what Steve actually writes is small (Session 8's counts); and porting takes the fun out (Steve). FOR THE EDITOR'S PASS NOW, not later: Part Two is in review.
