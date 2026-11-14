# The Shale book: its mission

This is the one home for the book's rules. Every writer and reader of the book, human or agent, reads it first. Each rule says why, and most quote Steve, the editor, with the date he gave it. The working title is *Automating agentic development with Shale*.

## 1. What the book is

**A. The premise.** Shale is a programming language written by agents, for agents. The reader learns to read it, and rarely writes it. Their agent writes it; they read it as it scrolls by, judge it, and steer the agent that wrote it.

**B. The reader.** An experienced developer who has written plenty of code, but not much with agents. They're curious about programming languages and open to letting an agent do real work. Vibe coders may come later. The opening states these expectations plainly.

**C. The story.** The book follows a side project: building a new essay surface from scratch, a small Go server with a plain-JavaScript front end, where the agent writes notes in markdown and Steve reads them and replies to any paragraph. The first two prompts have the roles turned around ("I write notes", "I read those comments"). Leave the prompts as written, and say the truth in the prose (Steve, 2026-11-14). It's told in numbered sessions, a chapter each, and a session is one stretch of work with one prompt. Say plainly, once, that the sessions aren't days: the real build took an afternoon of agent time, and the links feature came the next morning (Steve, 2026-11-14). It's an essay surface, not an agent harness, though the similarities are fair to point out: a human reads, replies and steers.

**D. The narrative is honest.** Tell an abridged story of what actually happened. Mistakes happen, and the book says so, each in an instructive context: what caught it, and what habit it taught. Abridge, but never invent, and never tidy a mistake away (Steve, 2026-11-13). The journals in the essay-surface repo are the record.

**E. We, and the prompts.** The book is narrated as "we": Steve and his agent. Each session opens with the prompt we gave the agent, quoted whole from `journal/prompts/`. The journals keep the weekday file names they were written under (`journal/monday.md`), since they're the record and the includes name them. Agents really did the work: "this isn't remotely dishonest" (Steve).

**F. Real work, how you should work.** The book walks the reader through real work, and its theme is "this is how you should work" (Steve). Programmers want to write apps, not books, so building a book is at most an aside.

## 2. What the book says about Shale

**G. Shale looks like Lua on purpose,** and it mostly works like any mainstream language: variables, functions, loops, lists and maps, strings and files. Its unique features are built for its niche, scripts an agent writes and a human reads. Those features are budgets, confinement and mounts, risky programs that need a reason, one-line stops, and a prelude for git, jobs, HTTP and refactors. Say this positively: no "works like nothing else" (Steve).

**H. Comparisons are welcome here.** The Roc book kept other languages out. This one names Lua honestly, uses bash as a foil, and doesn't push Shale. A later draft may even show Python recipes as valid tools for readers not ready for Shale (Steve).

**I. Shale is meant to have dialects.** Dialects cost humans much more than they cost agents, so each reader keeps the dialect that works for their agent, and builds their own library of scripts on top of the cookbook or instead of it. The language evolves deliberately, and your agent makes it better. The book shows a real feature arriving during the project. For now the reader forks their own copy. Say that lightly, since it may change.

**J. Shale builds nothing itself.** `go build` makes the server. Shale runs the build, keeps what it said, and judges what came out. Explain that nuance honestly.

**K. Gates early and throughout.** Gates are how we write quality software (Steve). They arrive when the work needs them, never contrived: a check before a commit in Session 1, then fast and full checks, planted faults, a release.

**L. Plants, honestly.** "Plant" is our word, so name the established ones. A permanent small input a check must reject or accept is a known-bad or known-good *fixture*. A fault put in once to prove a check catches it is *fault seeding*, or mutation testing. Fixtures can slow gates, often through needless rebuilds. Keep the book's examples fast, show what each costs, and say plainly why they're worth it: they catch tests that test nothing (Steve, 2026-11-13).

## 3. Quoting code

**M. Quote code, not header comments.** Steve rarely reads a script's opening comment. It helps an agent find the script, and the agent reads the whole script before running it. The human reads the code, so the code must be readable (Steve, 2026-11-13). Quote functions and lines, never a header. When a recipe is badly structured, polish the recipe before quoting it.

**N. Every quote comes from its file, at build time.**
- `{{file PATH}}` quotes a whole file.
- `{{function PATH NAME}}` quotes a Shale function.
- `{{lines PATH from "A" to "B"}}` quotes the lines between two anchors.
- `{{quote PATH}}` quotes a prompt as a block quote.
- `{{fence PATH "TEXT"}}` quotes the one fenced block of a journal that holds TEXT, verbatim, and `{{fence PATH "TEXT" from "A" to "B"}}` only its lines between two anchors.
- `{{show REPO REV:PATH}}` quotes a whole file as commit REV had it, for a script since changed or gone, and `{{show REPO REV:PATH from "A" to "B"}}` the lines between two anchors of it.

Each anchor must occur exactly once, so a quote can't drift. Never paste code that a file holds.

## 4. Voice

**O. Say what happens.** Write positive statements. Keep negatives rare, kept only where the "no" is the point (a guarantee, or a real caution).

**P. Strong, accurate verbs.** Code doesn't want, know, decide, trust or refuse in our sentences; it checks, stops, runs and keeps. The book shows or teaches, and it never reads: "here we learn".

**Q. Tight transitions.** Two sharp sentences often suffice between sections. No padding.

**R. At most 900 words of prose a chapter.** `book/build.shale` stops on more.

**S. American English, and plain words.**

**U. Mantras, not refrains.** A phrase repeated on purpose can be a mantra, and that's good: "green again ✅" may close a check that passes, as often as it's earned (Steve, 2026-11-14). A good mantra is short, because short is what makes it catchy: a few words, never a sentence. A phrase repeated by accident is a refrain, and it reads as padding. `book/refrains.txt` names each phrase with how many times the book may use it, and the build holds it there.

**V. One point per output block.** A quoted output block shows the lines that carry its point, and the sentence before it says the point. The build stops on an output block over 12 lines unless `book/long_blocks.txt` names it with a reason.

**X. Diagrams are drawn from source.** A ```dot fence in a chapter is drawn by Graphviz at build time, so a diagram changes with the words around it. The first is the loop, in "One loop". The story comes first: Shale's architecture (its interpreter, written in Zig, the host, the preludes) and most of its technical detail wait for after the sessions' narrative (Steve, 2026-11-14).

**Y. Sessions, not days (Steve, 2026-11-14).** The story's chapters are numbered sessions, not weekdays. That's more honest, and it doesn't cap the story at a week. A weekday in prose is a leftover of the old framing: `book/refrains.txt` holds each weekday to zero uses, and `monday` to the one sentence that explains the journals' names. Captions that name a journal file aren't prose, so they don't count.

**W. Chapters are named, never numbered,** in prose. A session has a number, so "Session 4" and "in session 4" are fine, and "chapter 4" is not. The build stops on "chapter 4".

**T. The essay surface is where we talk.** Decisions go in an essay, and Steve replies on the new surface itself.

**Z. A first version isn't about speed.** "For the first versions of an essay server, I would never be quite so obsessed with speed" (Steve, 2026-11-14). The early sessions keep only the numbers that carry a point. Speed becomes the subject only where the work truly calls for it, as when the checks for each moment get slow. Where the agent did measure something, the book can say so in a clause.

## 5. Working with agents

**AA. Show the agent side.** The reader is new to agents, so each session says who did the work: a cold agent with a small task, or the orchestrator. It also says roughly how long the work ran and how often we stepped in. Session 6 opens with the reply that started it, "go ahead with the jobs feature", and says plainly that the session began from a reply in conversation, not from a written prompt. Facts come from the journals and the session records, or they're left out.

**AB. Put yourself in the agent's shoes.** "Collaboration with an agent requires you putting yourself in the agent's shoes and knowing their strengths and weaknesses" (Steve, 2026-11-14).

**AC. The orchestrator is Steve's proxy.** Claude often acts as Steve's proxy in the orchestrator role. Cold agents do small tasks, and the orchestrator is largely responsible for coordination (Steve, 2026-11-14).

**AD. About 80%, and the creative exception.** Steve accepts the agent's recommendations about 80% of the time when he's being careful with his prompts. In creative work he's more opinionated, "because it's as impossible for an agent as it is for another human to guess what ideas your info will spark with me" (Steve, 2026-11-14).

**AE. Late-2026 agents, and principles that last.** The book is written with late-2026 agents, and agents are evolving quickly. That's two pitfalls: a reader may not know how much agents have already improved, and some of this workflow may be obsolete by 2027. Focus on principles that endure. Agents won't read minds, so communicating requirements is always part of the bottleneck, and that's part of what motivates the essay surface. The word "skills" appears exactly once, to debunk the need for skills files: modern agents acquired their skills in training, so we communicate missions and set standards instead, with mission documents, style guides and Shale gates. `book/build.shale` stops on any other count (Steve, 2026-11-14).

**AF. Memory is just files.** Steve lets agents manage their own memories. A memory file is a file the agent reads like everything else. The only special thing about it is that the harness loads the memory index into every session, so the agent sees it first. The agent writes memories with its file tools, and commits them with Shale (Steve, 2026-11-14; corrected for accuracy).

**AG. Tense from the reader's side.** The reader has never lived in Steve's ecosystem (Steve, 2026-11-14). Something the book hasn't shown yet is new to them, so it's introduced as new, never in a past tense that assumes shared history: "this book's project is an essay surface", not "that's why we built an essay surface" before the build is told. A story told in order may use the past tense once the reader is in it. The same rule covers insider names: a tool, file or habit is introduced before it's used.

**AH. Prompts carry missions and standards.** A session's first prompt usually establishes its mission. Follow-up prompts generally enforce standards, as with "add a defensive check" in One loop. Some prompts improve the workflow itself: the jobs feature, the child-run release. This is the concrete side of AE: the prompts are where missions and standards travel (Steve, 2026-11-14).

**AI. Every chapter teaches a lesson.** Each chapter, with the possible exception of the first two, has at least one concrete lesson: advice the reader can act on, stated as a rule, and earned by a moment in the chapter. A lesson takes an opinionated, authoritative tone: do this, never that, and why. Descriptions and other prose keep a more balanced tone (Steve, 2026-11-14). `book/personas/lessons.md` is the cold reader that checks this.

**AJ. Shale isn't a token saver.** Never portray Shale as something that saves tokens: it may add to token costs. It also slows you down at first, while you build up recipes. The book's other points matter far more (Steve, 2026-11-14). Meet Shale says this once, plainly, and then gives the main advantage over bash: Shale is a higher-level language, one in which you build up real primitives (Steve, 2026-11-14). There's no "trying this yourself" section with costs, since the book has no record of them.

## 6. Part Two

**PA. Part Two: four languages, one workflow.** Part Two makes the essay surface richer, with dot diagrams and small widgets in a note. Shale is the glue the agent builds, checks, refactors and ships with. Go does the plumbing, Roc expresses the features, and JavaScript does what JavaScript always does. Its premise is that true multi-lingual development is far more feasible with agents, for the same reason dialects are cheap for them. The book's central theme still holds: building your own workflow, now in app territory (Steve, 2026-11-14).

**PB. A format from first principles, not a port.** We don't port the markdown renderer to Roc. We declare up front that we're building something inspired by markdown, from first principles. It doesn't need all of markdown's power, and none of CommonMark's rigor. Markdown isn't a perfect fit for some of what we want, such as extending widgets. "Our book is more about building your own tools that work perfectly for you" (Steve, 2026-11-14).

**PC. Where Roc runs, organically.** Roc on the server, in the browser, or both: let requirements dictate. The book shows the choice being made when a feature forces it, never decided in advance (Steve, 2026-11-14).

**PD. Types as the prompt.** Each Part Two session opens with types: the prompt gives the mission, and the agent's first report is a proposed set of Roc types for Steve to reply to, before any code. This paradigm is new for us, precisely because our past work was so driven by porting. The book is honest about where we get the types wrong (Steve, 2026-11-14).

**PE. Read Roc, rarely write it.** Part Two mirrors Part One's premise: the reader reads Roc and judges it, and the agent writes it. A "Meet Roc" interlude opens Part Two, like Meet Shale, with one real function from the project. Roc's unusual features arrive only when a session needs them (Steve, 2026-11-14).

**PF. Watch the types-first session honestly (Steve, 2026-11-14).** The book records what really happens, including:
- whether the cold agent replies with Roc that compiles, or with a sketch of the types;
- whether it matters that the types compile right away;
- what happens if the agent gets bogged down in types before a minimum viable product, and whether that matters when we already know Roc can do the job;
- how the human is engaged for judgment and clarification without being bogged down in details.

These are observations, not a script. The answers come from the sessions.

**PG. The reader reads mainstream languages.** The reader knows how to read mainstream programming languages well enough to grok Roc. Frame Roc for that reader: show it, and explain only what a mainstream reader wouldn't guess (Steve, 2026-11-14).

**PH. Types can also be extracted.** Roc scripts already exist that extract the types from a working program (`roc-types`). That's retroactive, the reverse of types-first, and worth noting once Part Two is deeper in (Steve, 2026-11-14).

**PI. Prompt fidelity.** Part Two's prompts are drafted by the orchestrator close to how Steve prompts, and Steve doesn't need to approve each one. Perfect fidelity isn't the goal, since the reader's prompts won't match Steve's either (Steve, 2026-11-14).
