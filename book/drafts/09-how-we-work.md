# Draft

## How we work

Seven sessions of work leave habits behind. Here they are in one place, since they're the real subject of the book.

### Three ways to read

Most of the Shale you'll meet goes by quickly. Read it at three speeds:
- **At scroll speed,** a script going by: what does it read, what does it run, and how did it end? A Shale script names every program it runs, and its stop is one line, so a few seconds is enough.
- **At review speed,** a script the agent wants to keep: is it worth having again? Read its code, not its opening comment. Check its arguments, the way it fails, and its last line, which is its claim about what it did.
- **At trust speed,** a gate: what does it hold, how does its count stay honest, and what proves it's watching? A gate you've never seen fail is a gate you haven't tested.

### A check for each moment

By Session 5 the project had a check for every moment, each sized to how often it runs:

| When | The check | Its time |
|---|---|---|
| each edit | `fast.shale`: format, vet, the tests, a smoke test | about 3 s of CPU |
| before shipping | `full.shale`: fast, the race detector, the real notes | about 25 s |
| shipping | `release.shale`: full, then a checked swap with a rollback | about 27 s |

A check that's too slow stops getting run, so each one is held to a budget, and the budget is CPU time, which a busy machine can't break.

### Prove it can fail

Every gate on the list earned its place by failing once on purpose. In Session 3 a planted fault turned only three tests red, because one test's panic ended the run; once that test failed plainly, the same fault turned twelve red. In Session 4 one found a security hole that every stage had passed. Seeding faults costs a few minutes, once. Keep as permanent fixtures only the cheap ones: data a check reads, never something that needs a build of its own.

### Measure a baseline

Between Sessions 6 and 7, apart from any prompt, Shale's cookbook gained three Go hunts. They read code through Go's own parser and flag what a reader may want to look at twice: vague names, calls nested deep, and smells such as long functions. Each is proved by plants of its own. Run over essay-surface, they found little. There were no vague function names and nothing nested four calls deep. Two long functions in the markdown renderer, `inline` and `link`, are the next refactor if one is wanted. A hunt that finds little in good code is still worth running: it sets the baseline, and the next change gets held to it.

### Look with your own eyes

Every check passed in Session 2, and the reply buttons were invisible. Gates hold what you already know to check. A human's first look finds what you didn't know yet, and then the agent can often turn it into a gate.

### Working with agents

"Collaboration with an agent requires you putting yourself in the agent's shoes and knowing their strengths and weaknesses," Steve says. A cold agent knows only its brief and what it reads. It reads every doc it's pointed at, so a new word in Shale costs it little. It counts and measures tirelessly, sometimes past the point a first version needs. It guesses at names, as with `replace_all` in Session 7, and the stop that follows is one line. It can't see a page, so the hidden reply buttons in Session 2 waited for Steve's eye.

So the work splits in two. Cold agents do small, scoped tasks: each session's agent worked for between about ten minutes and half an hour. The orchestrator, Claude acting as Steve's proxy, does the coordinating. It wrote each prompt from his plan, passed on his replies in Session 4, ran Shale's gates before the merge in Session 6, and ran the releases.

Steve accepts the orchestrator's recommendations about 80% of the time when he's careful with his prompts. Creative work is the exception, and there he's more opinionated, "because it's as impossible for an agent as it is for another human to guess what ideas your info will spark with me." This book is creative work, and most of the rules in its mission are his words.

### Let the work ask

Nothing in this project was planned in advance. The checks, the release script and the language change all arrived because a chore came up more than once. That's how to grow a project's tools: make a script the third time you do something by hand, a gate once the script makes a judgment, and a change to the language once its gap starts to cost you.

### Your dialect

That last step is the one most languages don't offer. Shale changes as its agents work, and yours can too. Fork it, keep the dialect your agent works best in, and build your own library of recipes. The scripts in this book are a starting point. The ones your agent writes for your own work will be better.
