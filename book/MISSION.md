# The Shale book: its mission

This is the one home for the book's rules. Every writer and reader of the book, human or agent, reads it first. Each rule says why, and most quote Steve, the editor, with the date he gave it. The working title is *Automating agentic development with Shale*.

## 1. What the book is

**A. The premise.** Shale is a programming language written by agents, for agents. The reader learns to read it, and rarely writes it. Their agent writes it; they read it as it scrolls by, judge it, and steer the agent that wrote it.

**B. The reader.** An experienced developer who has written plenty of code, but not much with agents. They're curious about programming languages and open to letting an agent do real work. Vibe coders may come later. The opening states these expectations plainly.

**C. The story.** The book follows a side project: building a new essay surface from scratch, a small Go server with a plain-JavaScript front end, where the agent writes notes in markdown and Steve reads them and replies to any paragraph. Monday's and Tuesday's prompts have the roles turned around ("I write notes", "I read those comments"). Leave the prompts as written, and say the truth in the prose (Steve, 2026-11-14). It's told as a week, a day per chapter. Say plainly that the week is a side project, not five calendar days (the real build took one afternoon). It's an essay surface, not an agent harness, though the similarities are fair to point out: a human reads, replies and steers.

**D. The narrative is honest.** Tell an abridged story of what actually happened. Mistakes happen, and the book says so, each in an instructive context: what caught it, and what habit it taught. Abridge, but never invent, and never tidy a mistake away (Steve, 2026-11-13). The journals in the essay-surface repo are the record.

**E. We, and the prompts.** The book is narrated as "we": Steve and his agent. Each day opens with the prompt we gave the agent, quoted whole from `journal/prompts/<day>.md`. Agents really did the work: "this isn't remotely dishonest" (Steve).

**F. Real work, how you should work.** The book walks the reader through real work, and its theme is "this is how you should work" (Steve). Programmers want to write apps, not books, so building a book is at most an aside.

## 2. What the book says about Shale

**G. Shale looks like Lua on purpose,** and it mostly works like any mainstream language: variables, functions, loops, lists and maps, strings and files. Its unique features are built for its niche, scripts an agent writes and a human reads. Those features are budgets, confinement and mounts, risky programs that need a reason, one-line stops, and a prelude for git, jobs, HTTP and refactors. Say this positively: no "works like nothing else" (Steve).

**H. Comparisons are welcome here.** The Roc book kept other languages out. This one names Lua honestly, uses bash as a foil, and doesn't push Shale. A later draft may even show Python recipes as valid tools for readers not ready for Shale (Steve).

**I. Shale is meant to have dialects.** Dialects cost humans much more than they cost agents, so each reader keeps the dialect that works for their agent, and builds their own library of scripts on top of the cookbook or instead of it. The language evolves deliberately, and your agent makes it better. The book shows a real feature arriving during the week. For now the reader forks their own copy. Say that lightly, since it may change.

**J. Shale builds nothing itself.** `go build` makes the server. Shale runs the build, keeps what it said, and judges what came out. Explain that nuance honestly.

**K. Gates early and throughout.** Gates are how we write quality software (Steve). They arrive when the work needs them, never contrived: a check before a commit on Monday, then fast and full checks, planted faults, a release.

**L. Plants, honestly.** "Plant" is our word, so name the established ones. A permanent small input a check must reject or accept is a known-bad or known-good *fixture*. A fault put in once to prove a check catches it is *fault seeding*, or mutation testing. Fixtures can slow gates, often through needless rebuilds. Keep the book's examples fast, show what each costs, and say plainly why they're worth it: they catch tests that test nothing (Steve, 2026-11-13).

## 3. Quoting code

**M. Quote code, not header comments.** Steve rarely reads a script's opening comment. It helps an agent find the script, and the agent reads the whole script before running it. The human reads the code, so the code must be readable (Steve, 2026-11-13). Quote functions and lines, never a header. When a recipe is badly structured, polish the recipe before quoting it.

**N. Every quote comes from its file, at build time.**
- `{{file PATH}}` quotes a whole file.
- `{{function PATH NAME}}` quotes a Shale function.
- `{{lines PATH from "A" to "B"}}` quotes the lines between two anchors.
- `{{quote PATH}}` quotes a prompt as a block quote.
- `{{fence PATH "TEXT"}}` quotes the one fenced block of a journal that holds TEXT, verbatim.

Each anchor must occur exactly once, so a quote can't drift. Never paste code that a file holds.

## 4. Voice

**O. Say what happens.** Write positive statements. Keep negatives rare, kept only where the "no" is the point (a guarantee, or a real caution).

**P. Strong, accurate verbs.** Code doesn't want, know, decide, trust or refuse in our sentences; it checks, stops, runs and keeps. The book shows or teaches, and it never reads: "here we learn".

**Q. Tight transitions.** Two sharp sentences often suffice between sections. No padding.

**R. At most 900 words of prose a chapter.** `book/build.shale` stops on more.

**S. American English, and plain words.**

**U. Mantras, not refrains.** A phrase repeated on purpose can be a mantra, and that's good: "green again ✅" may close a check that passes, as often as it's earned (Steve, 2026-11-14). A good mantra is short, because short is what makes it catchy: a few words, never a sentence. A phrase repeated by accident is a refrain, and it reads as padding. `book/refrains.txt` names each phrase with how many times the book may use it, and the build holds it there.

**V. One point per output block.** A quoted output block shows the lines that carry its point, and the sentence before it says the point. The build stops on an output block over 12 lines unless `book/long_blocks.txt` names it with a reason.

**X. Diagrams are drawn from source.** A ```dot fence in a chapter is drawn by Graphviz at build time, so a diagram changes with the words around it. The first is the loop, in "One loop". The story comes first: Shale's architecture (its interpreter, written in Zig, the host, the preludes) and most of its technical detail wait for after the Monday-to-Saturday narrative (Steve, 2026-11-14).

**W. Chapters are named, never numbered,** in prose. The week's chapters are days. The build stops on "chapter 4".

**T. The essay surface is where we talk.** Decisions go in an essay, and Steve replies on the new surface itself.
