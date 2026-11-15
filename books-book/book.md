# Building a Book with Claude and Shale

This is a short how-to manual. It shows how the [Safari Ride book](https://roc.lynrummy.com/tutorial/), which teaches Roc through one real program, was built. Claude Code wrote the prose and the code, [Shale](https://github.com/showell/shale) scripts built the pages and checked them, and Steve Howell edited.

It's written for you if you have Claude Code and a GitHub account, can read a little Roc, and are curious enough about the machinery to look under the hood. The book starts Shale from the beginning.

The same machinery builds this book. Its source is one markdown file, `books-book/book.md`, on the [books-book branch](https://github.com/showell/shale/tree/books-book/books-book) of the Shale repository.

The first chapter gets you set up, with this book building on your own machine. The second shows how the book is a program. The third, the heart of the book, is the cadence of drafts and replies that finds a book's voice. The fourth covers working with several agents at once, and publishing.

## Getting set up

Start by having Claude set up your machine. You'll give it one prompt, it will fetch four repositories and install a few tools, and you'll finish with this book built on your own machine and open in your browser. Everything after this chapter builds on that setup.

### What you tell Claude first

Point Claude at Shale's README before anything else. The README says how to build Shale, how to set up its mounts and how to hook it into Claude Code, and it points to `docs/SHALE-AGENTS.md`, the guide written for an agent. Open a Claude Code session in an empty folder and give it a prompt like this one:

```text
Read https://github.com/showell/shale/blob/master/README.md, then
docs/SHALE-AGENTS.md in the same repo. Set up Shale here as the README says.
Clone these four repos side by side in this folder: shale, zig-webbox,
roc-apps and essay-surface, all from github.com/showell. Install Zig 0.17.0
and Graphviz. Write ~/.config/shale/mounts with four mounts: cookbook,
repos (this folder), build and scratch. Start a static web server on port
9210 over build's roc-apps/next/ folder. Then check out the books-book
branch of shale, run `shale books-book/build.shale` there, and tell me
what it printed.
```

### The four repositories

Claude clones all four into one folder, side by side, about 110 MB in all:

| repository | what it's for | its size |
|---|---|---|
| [shale](https://github.com/showell/shale) | the language, its cookbook of recipes, and this book on the `books-book` branch | about 900 files, 35 MB |
| [zig-webbox](https://github.com/showell/zig-webbox) | a library Shale builds against, checked out beside it | 42 files, 2 MB |
| [roc-apps](https://github.com/showell/roc-apps) | the Safari Ride program and its book, in `canvas_apps/safari/tutorial/`, whose builders and MISSION this book quotes | about 950 files, 66 MB |
| [essay-surface](https://github.com/showell/essay-surface) | the small web app where the agent's drafts meet the author's replies | about 100 files, 3 MB |

### What Claude installs

- **Zig 0.17.0**, the compiler Shale builds with.
- **Shale**, built by `zig build` in its checkout, with its `zig-out/bin` folder on your PATH.
- **Graphviz**, whose `dot` draws the book's diagrams.
- **A static web server** on port 9210 over the folder the build writes, so the pages open in a browser. Any server works.
- **A mounts file**, `~/.config/shale/mounts`. A mount names a folder outside the one a script runs in, and a script reaches it as `name:path`. The book's build uses four: `cookbook`, Shale's recipes; `repos`, the folder that holds the four checkouts; `build`, where the pages go; and `scratch`, for temporary files.

Shale runs on Linux on x86-64.

### What Shale is, and the hook

Shale is a small scripting language that looks like Lua, made for scripts an agent writes and a person reads. Every run has budgets, a program runs only with a timeout, a script reads and writes only inside its own folder and its mounts, and a script that goes wrong stops with one line that says where and why. Its recipes, scripts general enough for any project, live in the cookbook, and the [cookbook's README](https://github.com/showell/shale/blob/master/cookbook/README.md) lists each one in a line. The differences from Lua that catch a newcomer are in [SHALE-AGENTS.md](https://github.com/showell/shale/blob/master/docs/SHALE-AGENTS.md), under "If you write Lua".

The README also shows a Claude Code hook that sends every shell command Claude runs through Shale. Steve's machine runs with it on, so he reads one language as it scrolls by, each script's output stays bounded, and the agent builds up recipes that do a whole job in one launch. The hook is optional, and this book builds the same either way.

### How you'll know it worked

Ask Claude to run three checks and show you what each printed:

```text
shale --version
shale cookbook:prose/find_negatives.shale self_test=1
shale books-book/build.shale
```

The first prints `shale ReleaseSafe`. The second runs one of the cookbook's recipes on its own test cases, and passes. The third, run in the shale checkout on the `books-book` branch, builds this book: it prints a table of each page's words of prose, ends with `book: 6 pages answer at the dev channel's /books-book/`, and leaves the pages at http://localhost:9210/books-book/. Open that page in your browser, and you're reading this book, built on your machine.

## The book is a program

A book about code stays true only while its quotes match the code. The Safari Ride book keeps them matched by treating the book as a program. Its prose lives in a source file. A build turns that file into pages, and pulls each piece of code into the page from the real file at that moment. Checks called gates stop the build when something is wrong: a quote that points at a missing file, a chapter that runs long, a claim the compiler contradicts. You ran this book's build in the last chapter. This chapter opens it up.

### One source file

The whole book is one markdown file. Its `#` line is the title, the text under that line becomes the contents page, and each `##` heading starts a chapter. Here is a complete book in that shape:

{{file books-book/examples/tiny_book.md}}

The two lines in braces are includes. Each sits alone on its line and names a file. When the book builds, each include becomes a code block holding that file, or a part of it, as it stands that day. Edit the script and the next build quotes the edit, so the prose and the code move together. This book's builder knows five:

| include | what it becomes |
|---|---|
| `{{file PATH}}` | the whole file |
| `{{function PATH NAME}}` | one Shale function, with the comment above it |
| `{{lines PATH from "A" to "B"}}` | the lines from the one holding A through the one holding B |
| `{{runs PATH}}` | a script, and what it printed when the build ran it |
| `{{stops PATH}}` | a script, and the stop it hit |

A line range drifts as a file grows, so `{{lines}}` names two pieces of text instead. Each must appear in the file exactly once, and the builder checks:

{{function books-book/build.shale anchored_line}}

The check earns its place. When this book's builder was reorganized, the quote of its word limit, further down this page, stopped the next build: the line it was anchored to had been rewritten.

The Safari book's includes go further. Its builder reads Roc with Roc's own parser, so `{{code CatCrossing.cat_instant_at}}` quotes one definition by name, with its doc comment, wherever it sits in its module.

### The build

The build is the Shale script you ran, `books-book/build.shale`. It expands the includes, runs the gates, renders each chapter as a page, and writes the pages to the dev channel, the folder your web server shows.

```dot The build, from the source to the pages
digraph {
  node [shape=box, style=rounded];
  src [label="book.md"];
  files [label="the quoted files"];
  ex [label="the expanded book"];
  gates [label="the gates"];
  pages [label="the pages"];
  files -> ex [label="includes"];
  src -> ex;
  ex -> gates;
  gates -> pages [label="when all pass"];
}
```

### Gates that stop the build

A gate is a check that stops the build. Each gate here began as a mistake that repeated in an earlier book.

**Every include resolves.** An include that names a missing file, a missing function or an anchor found twice stops the build with a line that says which, and so does any other `{{` in the prose.

**At most 900 words of prose a page.** Words outside the code count. The limit keeps chapters short, and a chapter over it gets edited down to its key ideas:

{{lines books-book/build.shale from "local over = {}" to "if #over > 0 then fail("}}

**Say what happens.** The Safari book's strongest voice rule is to describe a thing by what it does. A sentence built on a negative word, such as `not`, `never` or `doesn't`, usually hides a plainer positive one. The cookbook's `prose/find_negatives.shale` finds every such sentence, the recipe you ran with `self_test=1` during setup. The build stops on each one until it's rewritten, or listed with its reason in `negatives.txt`. This book's table is empty.

**Claims checked against the compiler.** The Safari book teaches Roc's types partly through programs that fail to compile. "This fails" is a claim, so the build checks it. `{{fails turn_as_bearing}}` quotes `fails/turn_as_bearing.roc`, runs the pinned `roc check` on it, and quotes what Roc said. A program with zero errors stops the build, because the book's claim would then be false:

{{function repos:roc-apps/canvas_apps/safari/tutorial/build.shale fenced_failure}}

This book has the same kind of include for Shale. `{{stops PATH}}` runs a script and quotes how it stopped, and a script that runs to its end stops the build. Here is one that asks git for its newest commits and leaves out the timeout that `run` requires:

{{stops books-book/examples/run_without_timeout.shale}}

The first line names the problem and the rule behind it, and the `called from` line points at the script's own line 2. The agent reads a stop like this and fixes its script, and you usually read only the agent's one-sentence account of it.

### Contents, index and diagrams

The contents page is the text under the title, then each chapter with its sections, all read from the headings. Each chapter page carries the same list in a sidebar.

The index comes from the cookbook's `prose/build_term_index.shale`. It reads a terms file, one term a line with the patterns that find it, and writes a table for each category: each term's main chapter and every chapter that mentions it. It also reports smells, such as a term used several chapters before the one that teaches it, to the writer. This book's terms are in `books-book/index_terms.txt`.

A diagram is a fence tagged `dot`. The build hands its source to Graphviz and puts the drawing in the page, so a diagram changes with the words around it, as the one above does.

## Finding the voice with the agent

The machinery keeps a book true. A book's voice comes from a cadence of drafts, replies and rules between the author and the agent. In the Safari book, the agent wrote every sentence and Steve edited. This chapter is how that worked, step by step.

```dot The cadence, from the probe to a finished chapter
digraph {
  node [shape=box, style=rounded];
  probe [label="a probe"];
  spine [label="the spine"];
  draft [label="a draft"];
  gates [label="the gates"];
  cold [label="cold readers"];
  author [label="the author's replies"];
  mission [label="MISSION.md"];
  probe -> spine [label="reactions"];
  spine -> draft;
  draft -> gates -> cold -> author;
  author -> draft [label="one voice fixes"];
  author -> mission [label="new rules"];
  {rank=same; gates; cold; author;}
}
```

### A probe first

Start with a probe: a draft written to be thrown away. Ask the agent for a whole rough book in an hour or so, long enough to generate ideas and short enough for the author to read in one sitting. The author reacts, and the reactions find the book's real frame. The Safari book's lessons put it this way:

{{lines repos:roc-apps/canvas_apps/safari/tutorial/MISSION.md from "**Define the whole book's spine before drafting chapters.**" to "**Every book starts with a probe.**"}}

### Settle the spine

After the probe, agree on the spine before drafting the book you keep. A spine is a chapter written as one sentence per paragraph, each following from the one before by "so" or "but". It's cheap to fix, and a weak one costs a rewrite later. The Safari book's loop writes one for every chapter:

{{lines repos:roc-apps/canvas_apps/safari/tutorial/MISSION.md from "3. **The spine.**" to "3. **The spine.**"}}

### Prompt, essay, reply

Long-form work goes through an essay surface: a small web app that shows the agent's writing as pages, with a reply button on every paragraph. Steve's is [essay-surface](https://github.com/showell/essay-surface), a Go server with a JavaScript front end. The cadence has three steps:

1. The author gives a short prompt in the console: a chapter to draft, a question to think through, a decision to lay out.
2. The agent writes the draft or an essay as a note on the surface, and hands over its link.
3. The author reads it in the browser and replies on the paragraph each reply is about. The replies land in a file beside the note, where the agent reads them.

The agent writes every draft, and the author's words arrive as replies. A reply is precise: "this paragraph uses *sighting* before it says what one is" lands on that paragraph, and the agent fixes that paragraph. A decision goes in an essay too, with its options laid out, so the author's reply is a choice.

A pull request with review comments works the same way. The surface is faster: a note is a file the agent writes, and a reply is a line the agent reads.

### A MISSION file

A reply that generalizes becomes a rule, and the rules live in one file, `MISSION.md`, beside the book. Every writer and reader of the book, human or agent, reads it first, and the agent reads it again after each compaction of its context. A rule says why, and quotes the author with the date of the reply, so a later reader can weigh it:

{{lines repos:roc-apps/canvas_apps/safari/tutorial/MISSION.md from "**A7.**" to "**A7.**"}}

This book has one too, `books-book/MISSION.md`. Its rule B came from Steve's reply to this book's first draft, which opened with a Shale script, and it's why setup now comes first:

{{lines books-book/MISSION.md from "**B. Setup comes first" to "**B. Setup comes first"}}

The Safari MISSION groups its rules by axis: purpose, truth, order, voice, tone, vocabulary, the code, pictures and completeness. Each axis fails in its own way and takes its own kind of read.

### Cold readers with narrow briefs

A cold reader is a fresh agent that sees only the built chapter and its brief. Give each one a narrow job, because a reader asked to find everything finds the obvious. The Safari book used these:

| reader | its one job |
|---|---|
| truth and order | each sentence against the code, and each paragraph against only the pages before it |
| promise check | every promise the text makes, and where the book pays it |
| think-aloud | its understanding, paragraph by paragraph, so a break in the flow shows |
| voice | each sentence against the voice rules, naming the rule it breaks |
| nitpick | the five things a careful reader would stumble on, from a bare brief |

A nitpick round runs with a fresh agent each time, and the rounds stop when one finds only trivia, or after three.

### One voice

The readers report, and one voice phrases every fix:

{{lines repos:roc-apps/canvas_apps/safari/tutorial/MISSION.md from "**One voice.**" to "**One voice.**"}}

In practice the agent that wrote the chapter takes the findings, keeps the ones that fix something real, and rewrites in its own words. It fixes by subtraction first: cut the sentence, or move it, before adding one.

### A gate the moment a mistake repeats

A correction made once is a reply. When the same mistake comes back, write the check that catches it, in the same session, and add it to the build. A rule in MISSION depends on someone reading it, and a gate runs on every build. Three gates began this way:

- Steve kept striking negative sentences, so `find_negatives.shale` became a gate.
- Chapters kept growing, so the 900-word limit moved from a rule into the build.
- Diagrams kept arriving too dense to read, so the essay surface's `scripts/check_note.shale` stops a note whose diagram is wider than the page's column or holds more than ten nodes.

## Orchestration and publishing

Everything so far works in a single Claude Code session. This chapter covers the step past it, Claude orchestrating other agents, and then how a finished book reaches its readers.

### One session

In one session, the agent drafts a chapter, runs the build, posts the chapter as a note, and reads the author's replies. For a cold read, open a second session with the brief and the built page, and paste its report back. Start this way. The cadence makes the book, and more agents only make it faster.

### An orchestrator and its subagents

As the book grows, Claude can work as an orchestrator. It keeps the plan, writes the briefs, starts subagents with Claude Code's Agent tool, checks what they return, and keeps the author on one task at a time.

- **A lane** is a separate checkout on its own branch, where one agent works on one thing: a chapter's rewrite, a fix to a recipe, a new gate. A git worktree or a fresh clone both serve. This book's lane is a clone of the Shale repository on the `books-book` branch.
- **Cold readers run in parallel.** They only read, so several can run at once. Then one editor applies a triaged list of their findings, in one voice.
- **The brief carries everything.** A subagent knows only its brief, so name the files to read, the one thing to check and the shape of the report. Leave your own conclusion out of it, so the read stays independent.
- **The long jobs are the orchestrator's.** It runs the slow builds in the background, and keeps the subagents on quick checks.

### Publishing

The Safari book moves through three channels.

1. **Dev.** Every build writes to a stage of the dev channel, a folder a web server shows. A draft gets its own stage, so it leaves the main copy alone. This book's stage is `books-book`.
2. **Staging.** The Safari book's `book.shale` builds into a trial folder first. Then `book_check.mjs` drives each page in headless Chromium, and stops on a console error, a failed request, a widget that fails to run, a diagram that fails to draw, a broken link, or a page that runs past the window at desktop, tablet or print width. A book that passes replaces the channel's copy. Then `site/publish.sh tutorial` copies it into the roc-apps repository's `site/live/` folder and commits it, with a record of the compiler and the commit that built it.
3. **The public site.** A deploy copies staging to [the public site](https://roc.lynrummy.com/tutorial/).

Before each publish, the author reads every page once, as a reader would. The gates check what can be written down; the author checks what's left, the accuracy, the voice and the framing.

### Your own book

1. Pick a real program small enough to quote whole, and a reader.
2. Ask the agent for a probe, read it in one sitting, and react.
3. Agree on the spine.
4. Start `MISSION.md` with the rules your reactions produced.
5. Copy a builder. This book's `books-book/build.shale` needs only Shale and Graphviz.
6. Draft a chapter at a time: the build, the cold readers, one voice, your replies.
7. Add a gate each time a mistake repeats.
8. Publish when the gates pass and you've read every page.

The Safari Ride book's [tutorial folder](https://github.com/showell/roc-apps/tree/master/canvas_apps/safari/tutorial) holds everything this book describes: its MISSION, its source, its builders and its browser check. Read its MISSION's last section, "Lessons for the next book", before you start yours.
