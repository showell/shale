# Draft

## How we work

Seven sessions leave habits behind, and they're the real subject of the book.

### Three ways to read

Read Shale at three speeds:
- **At scroll speed,** a script going by, with Session 1's three questions. A Shale script names every program it runs, and its stop is one line, so a few seconds is enough.
- **At review speed,** a script the agent wants to keep: is it worth having again? Read its code, not its opening comment. Check its arguments, the way it fails, and its last line, which is its claim about what it did.
- **At trust speed,** a gate: what does it hold, how does its count stay honest, and what proves it's watching? A gate you've never seen fail is a gate you haven't tested.

### A check for each moment

By Session 5 the project had a check for every moment, each sized to how often it runs:

| When | The check | Its time |
|---|---|---|
| each edit | `check_before_commit.shale`: format, vet, the tests, a smoke test | about 3 s of CPU |
| before shipping | `check_before_release.shale`: the commit check, race detector, real notes | about 25 s |
| shipping | `release.shale`: that, then a checked swap | about 27 s |

A slow check stops getting run, so each is held to a CPU budget a busy machine can't break.

### Prove it can fail

Every gate on the list earned its place by failing once on purpose. In Session 3 a planted fault turned only three tests red, because one test's panic ended the run; once that test failed plainly, the same fault turned twelve red. In Session 4 a plant found a security hole every stage had passed. Seeding faults costs a few minutes, once. Keep as permanent fixtures only the cheap ones: data a check reads.

### Look with your own eyes

Every check passed in Session 2, and the reply buttons were invisible. Gates hold what you already know to check. A human's first look finds what you didn't know yet, and then you have the agent turn it into a gate.

### Four rules from the later sessions

The later sessions added four:
- **Hold the resource before you announce readiness.** In Session 5 a server said "listening" before its port was its own, and a check talked to the wrong server.
- **Run the gates of everything built on a primitive** when you change what it means. In Session 6 a new meaning for a stop reached every script with a job in it.
- **Copy your outputs aside before a change.** In Session 7, the new links were the only change: every real page matched its copy byte for byte.
- **A finder proves little until it has found something.** In Session 7 a finder found no bare URL left on the after pages. Run on the before pages, the same finder found all 26.

### Working with agents

"Collaboration with an agent requires you putting yourself in the agent's shoes and knowing their strengths and weaknesses," Steve says. A cold agent knows only its brief and what it reads. It counts and measures tirelessly, sometimes past what a first version needs. It can't see a page, so the hidden reply buttons in Session 2 waited for Steve's eye.

An advanced technique, separate from Shale, gets more done at once: split the work. Cold agents, each starting with only its brief, do small, scoped tasks, and an orchestrator coordinates them. It writes the prompts, passes on replies, and runs the merges and releases.

The orchestrator carries what lasts across sessions. It keeps that memory as files: one markdown file per fact, in a git repository, with an index the harness loads into every session. The agent reads a memory file like any other file, writes it with its file tools, and commits it with Shale.

Steve accepts the orchestrator's recommendations about 80% of the time when he's careful with his prompts. Creative work is the exception, and there he's more opinionated, "because it's as impossible for an agent as it is for another human to guess what ideas your info will spark with me." This book is creative work, and most of the rules in its mission are his words.

### When an agent looks sloppy

An agent that looks sloppy is usually being task-driven, and three different things look alike:
- **Drift:** a quality the task leaves unmeasured slides. Set a standard, and hold it with a gate.
- **Overreach:** a confident claim, stated before it's checked. Verify it.
- **Drive past a limit:** an agent blocked on its way to done finds a way around. State the limits up front.

A correction lasts once it lives in a file or a check, since tomorrow's cold agent starts from its brief alone. Newer agents may outgrow some of this. Overreach and drive are failings humans share.

### Let the work ask

This project's tooling grew out of the work. Steve asked for the checks and the release once their chores had come up by hand. That's how to grow a project's tools: make a script the third time you do something by hand, a gate once the script makes a judgment, and a change to the language once its gap starts to cost you.

### Your dialect

The scripts in this book are a starting point, and the ones your agent writes for your work will be better. Fork your own Shale, and let your agent grow its words and your library of scripts together, each added the third time your work asks for it.
