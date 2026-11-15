# Spike: five chapters from the kitchen

*2026-11-15. A first draft, somewhere between a column and a book, organized by kinds of work: each chapter a principle, an experiment from our own record, and a recipe worth keeping. As of this edition, every number here comes from one session's work; the code is quoted by hand from real files and runs, and would become includes in the book.*

## The spine: cadence

*Draft note, added after the landing page.* Every chapter runs on a cadence, and the book should say so out loud. A restaurant kitchen moves faster than we do, because there are plates to get out; our pace is closer to a dinner party's. But the kitchen is the better teacher, because its rhythm is a system: prep before service, the line during it, the pass at the end of it, and the knives sharpened after close. Our work has the same beats:

- **Prep is the cold agents.** They start from nothing but the brief, work in parallel, and hand back something ready to cook with. On the landing page, four of them each read the code and proposed a pitch for one program.

- **The line is the orchestrating agent.** It takes the prep, builds the dish, and keeps the station clean.

- **The pass is where the chef tastes.** Three more cold agents reviewed the page, and then you did, and your corrections were the ones that mattered most.

- **After close, the knives get sharpened.** Every rough spot the landing page hit in Shale became a fix the same morning, not a note for someday.

The landing page took about an hour from a dusty grid to shipped. That hour had all four beats, several times over, and each chapter below borrows one of them.

## Searching across files

### The kitchen

You're standing behind the agent while it works. It's halfway through a change and needs to know every place a function is called, across a few dozen files in three repos. It writes a short Shale script, launches it, and has its answer before you've finished reading the line it printed. A search like this happens dozens of times an hour. It's the kitchen's knife work: constant, unglamorous, and the first thing that goes wrong when the tools are dull.

### The principle

A search should hand back records, not text. A line of `grep` output is `path:line:text`, glued together, which the agent then has to pull apart again, and every agent does that a little differently. A Shale search hands back each hit as `{path, line, text}`, ready to count, sort or print. And a good search says what it skipped. A tree always holds something a search can't read: a link to elsewhere, a binary file, a name the filesystem allows and the store can't hold. A search that skips those quietly looks complete when it isn't.

### The experiment

**The myth:** `grep -rn` is good enough, so why replace it?

**What we did:** we read the agent's own transcript for one session of work, 1,670 Shale heredocs, and counted what they ran.

**What happened:** 399 of them, nearly a quarter, reached past Shale to run `grep` through `run`, each with a reason attached, such as "find how the server locates render-note." The agent wasn't careless; Shale's own `grep` searched one file, and a search across a tree meant a recipe launched on its own. So inside a heredoc, where the agent already was, `grep` was the shortest path.

**What changed:** the same afternoon, Shale grew `grep_tree`, a tree search a heredoc can call, and then refused `run` of `grep` outright. Here it is on the search that used to stop a recipe cold, a folder of the Zig compiler's tests with two file names too long for Shale's store:

```shale
local r = grep_tree("patched-zig-compiler/test/cases/compile_errors", "optional", {ext = {"zig"}})
print(#r.hits .. " hits in " .. r.files .. " files; passed " .. #r.passed)
```

*Shale, launched from bash: an excerpt of a real run.*

It printed `42 hits in 1014 files; passed 2`, and each file passed by came with its reason: a name the store can't hold.

### The recipe

The recipe worth keeping is `look.shale`, the cookbook's search for a launch line. It now runs on `grep_tree`, and it was checked against its old self, byte for byte, on four searches before the old walk was deleted.

### The lesson

**Ask for records, and ask what was skipped.** A search that returns text makes every caller parse it; a search that hides its skips makes every answer a guess.

### The cadence

*Draft note.* Search is mise en place. On the landing page, every claim was a search before it was a sentence: the code block on Safari's card came from a real file, the license line said UPL because the LICENSE files said UPL (you'd said GPL), and two claims came off the page because a search said no. A cook who checks the walk-in before writing the menu never has to 86 a dish mid-service.

## Changing many files at once

### The kitchen

The agent is renaming 67 recipes across 13 repos. You can hear it on the line: a burst of edits, a pause, a gate, another burst. Nobody's nervous, which is the interesting part, because a change this wide used to be a weekend of dread.

### The principle

An edit should fail loudly when its text isn't there. That one property makes a wide change safe: every edit either happened or stopped the run, so the change can't half-happen without saying so. Shale's `edit(p, old, new)` works that way, and `edits_all` plans every file of a change before writing any.

### The experiment

**The myth:** `sed -i` is a perfectly good way to change files.

**What we did:** counted the session's `sed` calls. There were 292, and 199 of them edited files in place.

**What happened:** `sed -i` does what it's told, and when the pattern doesn't match, it changes nothing and exits happily. The agent can't tell a skipped edit from a made one without reading the file again. Shale's `edit` was there all along, and stops the run naming the line it couldn't find.

**What changed:** Shale now refuses `sed` outright, even with a reason. The refusal is the lesson, in one line:

```text
`run` of `sed`, even with a want: `edit(p, old, new)` changes a file and stops the run when `old` isn't there
```

*Shale's stop, its first clause.*

### The recipe

For the rename, the agent wrote the standard down first, as a gate: `check_recipe_names.shale`, which holds every recipe name to a verb-first rule. The gate went in red, the renames turned it green, and it stays in the build, so the next recipe is born named well:

```text
check_recipe_names: cookbook: 0 of 86 recipes break the naming standard (72 runnable, 14 libraries)
```

*The gate's verdict, an excerpt.*

### The lesson

**Write the standard as a gate before the sweep.** The gate tells you when the sweep is done, and keeps it done.

### The cadence

*Draft note.* A wide change is where the prep line earns its keep. Cold agents can each take a slice in parallel, but only if the brief is good, because the brief is all they get: a prep cook who wasn't told the dish dices for the wrong one. The landing page's four pitch agents were briefed on the code and nothing else, so what came back was what the code could support, not what we hoped it said. Then the line, one agent, merged their work into one page, because a page, like a plate, needs one hand at the end.

## Gating a change

### The kitchen

The consumers gate has been running for nine minutes. It takes every repo that depends on Zig's web library and runs its checks against the change. The agent isn't waiting idly; it's drafting the next step, and it'll read the verdict the moment it lands. This is the pass, where every plate is checked before it goes out.

### The principle

A green is a claim, and a claim needs evidence that the checks you care about actually ran. A test suite can pass because the new test never ran: it was cached, filtered out, or never wired in. A gate worth trusting says how many checks ran, and the agent reads that number, not just the color.

### The experiment

**The myth:** a green build means your tests passed.

**What we did:** after adding a test for a new function, the agent read the build's summary line, which carried an aside:

```text
(2 test runs came from the cache and are not in the counts)
```

*The build's own note, an excerpt.*

**What happened:** the new test might have been one of the cached ones. So the agent ran the suite again, filtered to that test's name, and read `5/5 tests passed`: the test existed, ran and passed. That's a few seconds of doubt that turns a color into evidence.

**What changed:** nothing in the tools; this experiment changed a habit. When a test is new, the agent proves it ran.

### The recipe

`run_zig_build.shale` runs a build and prints what matters: the summary, each error once, the failed tests by name, and where the whole log went. Inside a heredoc, the same kind of verdict comes from `report_run`, which prints a line like `── exit 0 in 7 ms: git status --short` and stops the run when a program fails.

### The lesson

**Read how many ran, and that your change was among them.** A gate's color tells you it finished; its count tells you what it checked.

### The cadence

*Draft note.* The pass has two kinds of tasting. Machines taste for what can be written down: the consumers gate, the tests, an rsync that checked all 697 files of the landing page against staging before calling the deploy done. The chef tastes for what can't: on the landing page, you caught an intro that said too much, a lineage told through a name nobody knows, and a fragment where a sentence belonged. Neither tasting replaces the other. And the pass sends plates back fast; most of your corrections were one line, and the fix came back within a minute or two. That's the kitchen's speed, even at a dinner party's pace overall.

## Cleaning up a box

### The kitchen

It's the end of a long day, and someone says the walk-in is full of things nobody cooks anymore. Here the walk-in is a developer box with 37 repos, and one family of them, nine repos of a project now finished, has to go. The agent doesn't reach for a delete. It reaches for a map.

### The principle

Deletion is the last step, not the first. Before anything goes: know what's there, prove nothing exists only here, and fix whatever depends on what's leaving. Then delete, and gate again.

### The experiment

**The myth:** deleting is the easy part.

**What we did:** the agent mapped the box with a new recipe, `map_repos.shale`: each repo's branch, its last commit, its uncommitted files, how far it is from GitHub, and which other repos its README names. It ran in about two seconds:

```text
37 repos, 157 README links; recent = commits in the last 30 days (at most 1000 read)
```

*The map's last line.*

**What happened:** the map turned up one commit that existed only on this box, pushed it before anything was deleted, and found two live dependencies, a build file and a doc check, both fixed and gated first. And when the agent went to delete the last repo, Shale stopped it: a read-only mount still pointed there. That decision belonged to the person who made the mount, so it went back to them, and only then did the repo go.

**What changed:** nine repos and about 85 MB of build folders left the box, with nothing lost and every gate green.

### The recipe

`map_repos.shale` is the keeper. It's a few dozen lines of real logic, and it has already found its first surprise: one repo six commits behind its own GitHub copy.

### The lesson

**Map, prove, fix the dependents, then delete.** The deletion itself takes a second; everything before it is what makes it safe.

### The cadence

*Draft note.* Cleanup is after close, and after close is also when the knives get sharpened. The landing page rubbed against Shale in a handful of places: a 64-program cap that had been an irritation for weeks, a HEAD request that hung, a `..` path that stopped without saying what to do instead. Each became a fix the same morning, with its test, through the consumers gate. A kitchen that sharpens only when a knife breaks is a kitchen that cooks with dull knives most nights. The cleanup also kept something: Luke's copyright notice went into the repo beside his games, because cleaning the walk-in means labeling what stays, too.

## Reading your own agent

### The kitchen

The day's work is done, and the agent reads its own transcript: every command, every refusal, every retry, and every correction you made. It's like the kitchen going over the night's tickets: which dishes came back, which ones the line kept improvising, which tool everyone reached for and didn't have.

### The principle

An agent starts every session cold, and its transcript is the one record nobody tidied. Counting what's in it shows habits that no single slip reveals, because a single slip always feels like an accident.

### The experiment

**The myth:** a friendlier language for agents would accept more, such as aliases for the names agents guess.

**What we did:** a new recipe, `count_shale_stops.shale`, counted one session's launches:

```text
   n  what
1371  heredoc, ran
  87  heredoc, stopped by Shale
  80  heredoc, stopped by its own `fail` (a check's verdict)
```

*The census, its first rows.*

**What happened:** most stops were habits from other languages: a `write` or `map` that Shale doesn't have, a Lua loop with two names. The most stubborn was a function reaching for a script's variable, which Shale doesn't allow; the agent hit that one four times in one afternoon. An alias would have hidden each guess, and some guesses would have run with their home language's meaning, quietly wrong.

**What changed:** the rule stayed, by choice: strictness over convenience. The convenience moved into the stop, which now names the exact fix:

```text
`today` is a script `local` set once ... declare it `const today = ...` where it is set
```

*Shale's stop, shortened.*

### The recipe

`count_shale_stops.shale` is the keeper. Run it once a week. That cadence is the point: often enough to catch a habit while it's fresh, rarely enough that you spend your time building the house and not polishing the tools.

### The lesson

**Count before you change the language.** The census decides what Shale grows next, and the stop is where the language teaches.

### The cadence

*Draft note.* Going over the tickets is the slowest beat, and the one that sets the next night's pace. Your corrections on the landing page didn't stay corrections: "no fragments" went into memory as a rule, so the next session starts with it, the way a kitchen's family meal is where the chef says what went wrong last night. A cold agent never hears the chef directly. It hears the brief, and the brief is only as good as what the line wrote down after the last service.
