# Reading Shale

Shale is a programming language written by agents, for agents. This book is for the humans beside them: people curious about programming languages and open to letting an agent do real work. You will read a lot of Shale and write very little. Here we learn to read it at the speed it scrolls by, to judge the recipes an agent keeps, and to trust the gates that hold a codebase, or a book, to its standards.

Every script quoted from a file is pulled from it when the book is built, so each one is the script as it stands today. The short examples typed into the text are unchecked so far. This draft is rough: eight chapters, prose and code, with pictures still to come.

## A book published by a script

Shale is a programming language written by agents, for agents. It looks a lot like Lua, on purpose: an agent reads it at a glance, and so can you. Agents write Shale every day on the machine where this book was written. They build with it, test with it, rename across a tree with it, and publish with it. A human reads it as it scrolls by.

That human is you, and this book is about reading. The premise is unusual: you will learn a language you almost never write. Your agent writes it. You read it, judge it, and steer the agent that wrote it.

### This morning's deploy

The best way to meet Shale is to watch it do something real. This morning a 39-chapter book about the Roc language went to staging. Before it could, one Shale script had to say it was ready. Here is the part of that script that matters:

{{file repos:roc-apps/canvas_apps/safari/gates/publish.shale:13-39}}

If you know Lua, or Python, or JavaScript, most of this reads itself. `const` names a value. `function` starts a function, and `end` closes it. `..` joins strings. `~=` means "not equal". `run` starts a program, and two kinds of option appear on every call: `timeout_ms`, how long the program may take, and a keep, how much of its output comes back (`tail` for the last lines, `whole` for all of it up to a size).

`main` runs three stages. The first is the merge gate, from cold caches. The second is `check.shale`, which runs the compiler over 59 apps. The third builds the book and checks every link in it. After each stage, `report_stage` prints what the stage said, and how long it took. Then it asks one question: did the stage end well? If the stage failed, `fail` stops the whole run.

This morning, every stage passed. The merge gate took 120 seconds, the compiler check 28, and the book 148, including a browser that checked 2,214 links. The last line printed was the verdict:

```text
safari publish: every stage passed; site/publish.sh takes each page from the dev channel
```

### Gates

`publish.shale` is a gate, and it's made of gates. A **gate** is a script that holds work to a standard: it runs the checks, prints a verdict on its last line, and fails when the work falls short. Gates are how we write quality software. They're also how we keep a book readable and accurate, and this book will show you gates that do exactly that, for prose as well as code.

Gates run through every chapter here, because they are where Shale earns its keep. A one-off script answers a question once. A gate answers it on every change, forever, and turns a rule into a number that can't quietly slip.

### The ladder

Gates don't appear from nowhere. On this machine they grow, one rung at a time:

1. **A heredoc.** An agent needs an answer once, writes ten lines of Shale, runs them, and reads the output.
2. **A recipe.** The question comes back, so the ten lines get a file, a name and arguments.
3. **A recipe with plants.** The recipe starts making judgments, and each judgment gets proof: small files that must trip it, and small files that must stay quiet.
4. **A gate.** The judgment becomes a count, held on every commit.

The book climbs that ladder in order, with one detour in chapter 4 to the recipes that change code. The last two chapters put the gates to work, on a book and on a day. Each rung asks something different of you as a reader. A heredoc goes by at scroll speed, and you want to know what it touched. A recipe asks for review: is it worth keeping? A gate asks for trust, and you give it only once you've seen what the gate holds and what proves it's watching.

### Who wrote this language

Agents wrote Shale: the interpreter, the libraries and most of the recipes. A human set the taste and said no, often. Both of those show in the code. It is plain, it explains itself in comments, and it stops loudly when something is wrong. Each of those matters to a reader, as we'll see. The next chapter starts where an agent's work does: a heredoc scrolling by.

## Reading a heredoc as it scrolls by

On this machine, a hook stands between an agent and the shell. Every command the agent sends goes to the hook first, and the hook lets one form through: a Shale launch, plus a logged escape we'll meet in the next chapter. So what scrolls by in an agent's session looks like this:

```shale
cd ~/showell_repos/shale && shale <<'EOF'
local hits = 0
for p in glob("src/**/*.zig") do
  for g in grep(p, "job_alive") do
    print(p .. ":" .. g.line .. ": " .. g.text)
    hits = hits + 1
  end
end
print(hits .. " lines hold job_alive")
EOF
```

The first line says where the script runs, and the lines between `<<'EOF'` and `EOF` are the script. This one finds every line that holds `job_alive` in the project's Zig files. `glob` lists the files, `grep` gives each matching line with its number, and the script prints them, then a count.

### Three questions at scroll speed

A script going by gets a few seconds of your attention. Spend them on three questions:

- **What does it read?** Here: the Zig files under `src`, and nothing else. A Shale script reads only under the folder it runs in, and its mounts. A mount is a folder elsewhere with a short name: `cookbook:look.shale` is `look.shale` in the cookbook's folder, and `build:`, `scratch:` and `repos:` name the build, scratch and repositories folders.
- **What does it run?** Here: no programs at all. Every program a script runs goes through `run`, `job_start` for one that outlives the script, or a prelude call like `git_commit`, so a program is easy to spot.
- **How did it end?** Shale prints a footer when a run stops, and the footer says where and why.

Here is a program being run, as you'll see it many times a day:

```shale
print(must(run({"git", "log", "--oneline"}, {timeout_ms = 10000, tail = 3})).out)
```

`run` takes the program as a list of words, so there is no shell quoting to read through. It requires a timeout and a keep: here, ten seconds, and the last three lines. A hung test gets stopped, and a megabyte of build log comes back as its tail. `must` turns a failed program into a failed script. Every run also has budgets for steps, reads and output. Those guard rails are table stakes. You'll stop noticing them, and that's the point.

### The shape of a stop

When a script goes wrong, Shale stops it with one line. These are real, from the afternoon this chapter was written:

```text
── stopped at stdin:4:12: name: no function `range_list` above this line: a function is called only after it is declared (0 steps)
── stopped at stdin:2:6: refused: several loop names are not in this language: `for k in keys(m) do ... m[k] ... end` (0 steps)
```

Each names the place, a class (`name`, `refused`) and the reason. The second even suggests the fix. An agent reads that line and tries again, and so can you. A stop costs a retry, and the line tells you exactly which retry.

### Familiar on purpose

Shale borrows Lua's look so that an agent's habits, and yours, carry over. The differences are deliberate, and each one makes a script easier to read in a few seconds:

- **A function is declared above where it's called,** at the top level, and closures are out. You read a script top to bottom, once.
- **A missing key stops the run.** In Lua it's a quiet `nil` that surfaces three steps later.
- **Patterns are regular expressions,** a subset of the ones you know that always runs in linear time, in place of Lua's own patterns.
- **Integers only.** No floats, so no rounding to wonder about.
- **One loop name per `for`.** You saw that stop above.

### Bash, honestly

Shale replaces bash, `sed`, `grep` and Python on this machine, and sometimes the shell is shorter. Here is a count of a code comment's reasons, both ways:

```shale
local by = {}
for p in glob("src/tcp*.zig") do
  for m in grep(p, "DEPARTS (") do
    local r = re_find(m.text, "DEPARTS \\((\\w+)\\)")
    if r ~= nil then by[r.group] = get(by, r.group, 0) + 1 end
  end
end
for k in keys(by) do print(k, by[k]) end
```

```sh
grep -h 'DEPARTS (' src/tcp*.zig | sed -E 's/.*DEPARTS \(([a-z]+)\).*/\1/' | sort | uniq -c
```

The shell wins on length, and for a question asked once, that's a fine answer. The Shale version names each step and says how it ended, which starts to matter when a script like this comes back. That's the next chapter.

### The smallest gate

`must` is a gate in miniature. It asks a program "how did you end?" and stops the script when the answer is bad. Every gate in this book is that question, asked more carefully, on more things.

## A recipe is a heredoc that came back

Most heredocs are asked once and forgotten. Some come back. When an agent writes the same ten lines for the third time, those lines earn a file, a name and arguments. That file is a **recipe**.

Recipes start in the repository that uses them, in its own scripts folder. A recipe moves to the shared cookbook once it has proven itself and a second repository wants it. Today the cookbook holds about 60 recipes, with the libraries they share, and about 75 more Shale scripts live in the other repositories on this machine.

### A whole recipe

Here is a small recipe in full. It renames a word across a project, in any language:

{{file cookbook/rename_words.shale}}

### Reading it at review speed

A recipe asks more of you than a heredoc. The agent proposes to keep it, so you're deciding whether it's worth keeping. Read it in this order:

**The header first.** The opening comment is the recipe's contract: what it does, how to launch it, and its limits. This one is honest about its main limit: "strings and comments are words too: read the summaries, then `git diff`." A good header tells you what to check after.

**Then the arguments.** Each `arg("from", "")` reads a `name=value` from the launch line, with a default. Shale checks the launch against them before the script starts, so `form=fetchUser` stops the run with a list of the names the script reads. A misspelled argument never runs quietly with its default.

**Then the work.** `walk_all` lists the files, `grep_all` keeps the ones that hold the word, and `edit` makes the change: every whole word, one write a file. `attempt` runs `rename_in` and gives back its result or its failure, with a `class`. An `argument` failure means the word appears only inside longer words, so the file is listed and left alone. Any other failure stops the run.

**Then the last line.** `print(FROM .. " to " .. TO .. ": " .. renamed .. " in " .. files .. " files")` is the recipe's claim about what it did.

### Where recipes come from

The hook leaves one way past it: a command whose first line is `# want: tag: why`, meaning Shale should do this, or `# out-of-scope: tag: why`, meaning it never will. The command runs, and a line goes into a log. A recipe reads that log back:

{{file cookbook/wants.shale:1-19}}

That log is where many recipes start. When the same want shows up again and again, the habit behind it becomes a recipe, and the escapes stop. The language grows toward what its agents actually reach for.

### A summary is a claim

"fetchUser to loadUser: 12 in 4 files" is a sentence, and sentences can be wrong. At review speed, ask what checks it. For a rename, `git diff` shows every change, and `git checkout` takes it back. `cookbook/syntax.shale` checks that each changed file still parses. Those are checks someone has to remember to run, though. The next rung up is a recipe that checks its own judgments, and before that, recipes that change code with more care than a word match.

## Refactoring with the language's own tools

A word rename treats code as text. That's fine for a doc, and risky for code. `sed` renames a word in a string, in a comment, in a field of another type, and inside a quoted identifier. Here's the habit Shale replaces:

```sh
grep -rlw pastDeadline src | xargs sed -i 's/\bpastDeadline\b/deadlinePassed/g' && git diff
```

### A rename that reads tokens

Shale has builtins that read code the way its compiler does. `zig_rename` uses Zig's own tokenizer:

```shale
local n = 0
for p in glob("src/**/*.zig") do
  if #grep(p, "pastDeadline") > 0 then
    local r = zig_rename(p, "pastDeadline", "deadlinePassed")
    n = n + r.renamed
    if r.left_in_comments > 0 then print(p .. ": " .. r.left_in_comments .. " left in comments") end
    if #grep(p, "timeouts.pastDeadline") > 0 then edit(p, "timeouts.pastDeadline", "timeouts.deadlinePassed", {all = true}) end
  end
end
print(n .. " renamed")
```

It renames identifier tokens and nothing else. A use after a `.`, like `timeouts.pastDeadline`, gets its own `edit`, and the build judges whatever is left. A string, a test's name or another struct's field stays as it was. Mentions in comments are counted and left for a human to decide. Roc gets the same treatment from its own parser: `roc_tree` hands a recipe the parse tree of each file, and the Roc hunts in the cookbook read names, imports and nesting from it.

### Edits that pin their text down

The everyday change is `edit(path, old, new)`. It replaces a literal, prints a one-line summary of the diff, and fails unless `old` is there exactly once, or `{all = true}` asks for every one. Its list form takes several edits to one file. Each `old` must occur exactly once, in the file as the edit before left it. The file gets one write, or none if any edit fails. A half-applied refactor is never left behind.

### Merges, with a library

Merge conflicts are where agents used to write their most fragile code: hand-rolled scans for `<<<<<<<`. Now there's a library for it, which a recipe loads with `use("cookbook:conflicts.shale")`. Its smallest function shows the style:

{{function cookbook/conflicts.shale conflict_marker_kind}}

It reads one line and names the marker it is, CRLF files included. The library's other functions build on this one. `git_merge` in the git prelude does its part too. On a conflict it aborts the merge and stops, naming the files, so a checkout is never left half merged.

### The build judges the move

Every refactoring recipe ends the same way: something else judges it. After a rename or a move, the compiler builds the tree. After moving Zig declarations between files, `cookbook/zig/move_judge.shale` checks the result, and `same_tests.shale` checks that every test still exists by name. The conflicts library has its own self-test, `conflicts_check.shale`. A refactoring tool is only as good as the judge behind it. In the next chapter, the judges themselves get judged.

## Plants: proving a judge is watching

Some recipes change code. Others judge it: this name is vague, this import is never used, this function nests too deeply. A judging recipe prints findings, and a gate can hold their count. But a judge has a failure mode the last chapter's tools lack. It can pass because it's blind.

### A judge

Here is the top of one, which finds Roc imports a file never uses:

{{file cookbook/roc/unused_imports.shale:1-25}}

The header names one check, `unused_import`, and defines exactly what counts as a use. That precision matters, because the recipe will be held to it.

### Plants

A **plant** is a small file made to test one judgment. Some plants must trip the check. This one imports three modules and uses two:

{{file cookbook/roc/plants/unused_imports/unused.roc}}

Others must stay quiet. Their names start with `quiet_`, and each holds a case the check could get wrong. This one uses every import, in three different ways:

{{file cookbook/roc/plants/unused_imports/quiet_qualified.roc}}

You don't need to know Roc to read these. A plant is a tiny file and a verdict: "this must fire once" or "this must say nothing".

### The table

The recipe's self-test lists every plant with what it must produce:

{{file cookbook/roc/unused_imports.shale:143-161}}

A row with `counts` must print exactly those findings and exit nonzero. A row with `says` must print those words. A row with neither is a clean case: exit 0, no findings. The last row points at files that don't parse, and the recipe must stop on them by name.

### The function that checks the plants

The Roc judges share their self-test machinery in one library:

{{function cookbook/roc/gate_recipe.shale check_plants}}

Read the second loop. A check that no plant makes fire fails the self-test, "since a check no plant fires could be blind." Both sides get asserted: each check fires where it should, and stays quiet where it should.

### How many

The cookbook holds about 600 plant files. The name hunt alone has 148, one or more for each naming rule it enforces, and each named for the rule it holds. When a new rule arrives, its plants arrive with it.

### The self-tests are a gate

Shale's own build has a step, `cookbook-test`, that runs every recipe's self-test. A recipe that judges wrongly breaks Shale's build. So when an agent proposes a judging recipe, the review question is simple: where are its plants? A judge with plants on both sides is ready to become a gate.

## A gate holds a count

The Roc book that went to staging this morning has a rule for its prose: say what happens, and never what something isn't. "The rods never change" becomes "the rods stay the same". It's a good rule and an easy one to drift from. Over 39 chapters and many agents, it would have drifted, so it became a gate.

### The words

The lint starts from a list:

{{file repos:roc-apps/canvas_apps/safari/gates/book_negatives.shale:58-63}}

A sentence that holds one of these words, as a whole word, is a negative. Inline code is taken out first, so a quoted `doesn't` never counts.

### The exceptions table

Some negatives could belong in the book. A sentence like "Roc forbids import cycles, so no path of arrows loops back" states a rule the compiler enforces, and the "no" is the point. So the lint reads a table of kept sentences, each with its kind. Today the table keeps none. Its `fix` kinds name the negatives that must be rewritten, so a sentence of that kind can't be parked in the table. The header defines the table's rules:

{{file repos:roc-apps/canvas_apps/safari/gates/book_negatives.shale:33-48}}

Look at `unused_exception`. A kept sentence that the prose no longer holds is a finding too. An exceptions list usually rots. This one gets checked from both sides, as the plants were.

### Held

Here is the verdict, the last lines of the script:

{{file repos:roc-apps/canvas_apps/safari/gates/book_negatives.shale:204-216}}

`held` is the number the merge gate expects. With `held=0`, the run passes only when the count is exactly zero. Held at three, it would pass at exactly three: four fails, and so does two, until someone lowers the number. So the held number changes only on purpose, in a commit someone reads. The comment above `VERDICT` explains a small craft detail: the `fail` stands at the top level, so the summary is the last line a caller sees.

This morning it printed:

```text
book_negatives: negative 0, unused_exception 0, 0 negative sentences of 2015 read above `Appendix: the patterns`, 0 kept in the table, as held
```

Over two thousand sentences, read on every merge, in a fraction of a second.

### A rule in a day

Rules arrive from people. On the day the Roc book shipped, its editor set a limit: no chapter over 900 words of prose. The same day, the book had a gate for it:

{{file repos:roc-apps/canvas_apps/safari/tutorial/book_stats.shale:149-152}}

The rule went from a sentence in a conversation to a failing build the same day. That's the whole case for gates in one example. Rules that live in a style guide get remembered sometimes. Rules that live in a gate get applied every time.

### Reading a gate at trust speed

When you meet a gate, ask three things. What does it hold? Here, a count of zero. How is the count kept honest? Here, by the exceptions table, checked from both sides. What proves the gate is watching? For a recipe in the cookbook, its plants. The book's lints have no plants yet. Today the only evidence is that every sentence ever fixed was a finding first, and by chapter 5's standard that's weak. Plants for the book's lints are on the list.

The negatives were about voice. The next chapter's gates are about truth.

## A book whose code can't go stale

A book about code makes two kinds of claims. It quotes code, and it talks about code. Both go stale as the code moves. A Shale-built book can make both kinds of claim checkable.

### Quotes by name

The Roc book's source is prose with lines like `{{code CatCrossing.cat_instant_at}}`. Its build script replaces each one with the definition as the source has it today:

{{file repos:roc-apps/canvas_apps/safari/tutorial/build.shale:4-36}}

Every include is a check. A renamed function stops the build. An anchor that no longer occurs exactly once stops the build. And `{{fails turn_as_bearing}}` holds a sentence to the truth: the book says this program fails to compile, so the build compiles it, and if the compiler finds no error, the build stops. The book's claim would be false.

### This book, too

The book you're reading is built the same way, more simply. Its source quotes files and functions by name, and its build fills them in:

{{function book/build.shale function_of}}

It finds the function, walks up over the comment above it, and down to the first `end` at the start of a line. A function that's gone stops the build. So every quoted function in this book is the code as it stood the day the book was built. Line ranges are weaker: the build checks only that the lines exist, so an edit above them quietly shifts the quote. Roc's book anchors its excerpts on text, and this book should too.

### Names in sentences

Includes can't go stale, but sentences can. "`Tour.step_back` takes one step back" is prose, and a rename leaves it wrong. So the Roc book has a second lint:

{{file repos:roc-apps/canvas_apps/safari/gates/book_names.shale:2-4}}

It reads every backticked name in the prose, looks for it in the code, and holds the count of missing ones at zero. A table lists the names that rightly live elsewhere, each with a reason, checked from both sides like the negatives table.

### A book that reports on itself

Other scripts read the book for its shape. `book_stats.shale` counts each chapter's prose, code, diagrams and pictures. `book_index.shale` builds the index from a list of terms, and flags a term that may be used before the chapter that teaches it. Neither one writes a sentence. They hold up a mirror, and the author decides what to fix.

### What this adds up to

A book built this way keeps the promises a book can't usually keep. Its code is current. Its claims about failing programs are checked. Its names point at things that exist. Its chapters stay short. Each of those is a small Shale script, and each started as a heredoc someone ran once. The last chapter puts them in order.

## From one edit to publish

Gates cost time, and a gate that's too slow stops getting run. So the gates on this machine are arranged by cost, in tiers, each for a different moment.

### Three tiers

**The fast tier runs on each edit.** For the Roc book's game, that's `fast.shale`:

{{file repos:roc-apps/canvas_apps/safari/gates/fast.shale:1-13}}

About ten seconds: the compiler's check, the formatter, the main tests, the lints on the changed files, and one frame held to its hash.

**The merge gate runs before each push.** It runs everything, and reports every failure:

{{file repos:roc-apps/canvas_apps/safari/gates/run.shale:1-33}}

About two minutes warm. Each part is a child run of the same script, so one part also runs alone when that's all you need.

**The publish tier runs before the site moves.** That's the script from the first chapter: the merge gate again, from cold caches, then the compiler over every app, then the book.

### Slowness is a finding

Look at the merge gate's header again: "Each stage has a CPU budget, about twice its warm time … since a slow gate is a fault to find." A stage that takes twice as long as it should stops the run, the same as a stage that fails. The budget is CPU time, because wall time doubles on a busy machine and would make the gate flaky. A small library, `cookbook/times.shale`, keeps each stage's last five runs and prints a line when one changes a lot:

```text
build.sh safari: 192 s, was 35 s (5.5x)
```

The agent reads that line and finds out why. Because the gates stay fast, they get run on every edit, push and publish.

### The day, end to end

Here is a day's work on the Roc book, as Shale saw it. An agent edits a chapter, and the fast tier answers in seconds. It commits, and the merge gate runs every part: the lints, 1,014 tests, 20 widgets checked and built, the book rebuilt with its includes, the names and negatives held at zero, and the frames drawn and compared. It pushes. When the editor says ship, the publish tier runs it all again from cold, and checks 2,214 links in a real browser. The last line says every stage passed.

A human read all of that as it scrolled by. Not every line: the verdicts.

### The three speeds, together

That's the habit this book has been building. A heredoc scrolls by, and you ask what it read, what it ran, and how it ended. A recipe asks to be kept, and you check its header, its arguments and its last line. A gate asks for your trust, and you read what it holds, how the count stays honest, and which plants prove it's watching.

You won't write much Shale. You'll read a great deal of it, and you'll decide which of it becomes a gate. That decision is how an agent's work gets held to your standards, every time, whether you're watching or not.
