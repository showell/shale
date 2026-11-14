# Draft

## Session 9: a demo, end to end

The orchestrator drafted this prompt from Steve's plan, and it opens with his answers from the last session:

{{quote repos:essay-surface/journal/prompts/demo.md}}

A cold agent took it at about 14:01 and finished in about twenty minutes. Steve didn't step in. The orchestrator ran the release about forty minutes later.

### Look before you choose

The prompt leaves where Roc runs to the demo. First, the agent looked for Roc already in use, and for Graphviz and node:

{{fence repos:essay-surface/journal/demo.md "def render_with_roc"}}

The old surface already runs Roc in production: a plain app on the platform Roc ships with, which gives it arguments and stdout, run once per note with the note's text as its arguments. A probe at the pinned compiler built a tiny app in 131 ms and showed that `Json.to_str` is a builtin, so a Roc program can answer in JSON without an escaper of its own.

### Where Roc runs

The agent chose the arrangement that already runs on Steve's server. `format/render_note.roc` is built into a program, `render-note`, beside the server. The Go store runs it each time a `.note` changes, with the text as arguments in parts of at most 32,000 bytes, and keeps the page it prints. A request never runs Roc.

The reasons are requirements, not taste. A platform of our own would mean writing its host program in Zig, built against the same compiler version, and the copy of the compiler's source on Steve's server was at another version. The seam is one JSON value. And a note renders when it changes, so a process per render costs a reader nothing; the demo renders in about 2 ms.

The prompt asked for the costs too, and the agent named four:
- **Arguments are the only way in.** The platform gives no stdin and no file reads. Linux's argument space here is 4,194,304 bytes, about twenty-five times the largest real note, but it's a ceiling.
- **Two things to deploy.** `release.shale` built only the Go binary. The agent flagged it and left it, since it couldn't run a release to prove a fix.
- **A process per note at start,** the first thing to measure if most notes move.
- **The browser can't run it.** Widgets, or a live preview, need Roc in the page, which is a different arrangement.

That last cost stays small, because the types, `Parse` and `Page` are pure and would move unchanged. Only these lines belong to the platform:

{{file repos:essay-surface/format/render_note.roc:6-18}}

Let a requirement place your code, never a plan made before the feature, and keep the part that depends on the place small.

### Four stops, then a first-run render

The toolchain took about three minutes in all, and never fought. The compiler stopped the agent four times, each on the agent's own Roc: `List.take_while` doesn't exist at the pin, twice a record pattern in a `match` lacked its `..`, which a record pattern needs to ignore the fields it doesn't name, and an assignment can't be a match arm's whole body. That last stop is why the loop in `Text.from_spans` asks each span whether it's unstyled:

{{lines repos:essay-surface/format/Text.roc from "from_spans = |given| {" to "Ok(Text.(given))"}}

Once it compiled, the demo note rendered right on the first run. Here's its diagram, drawn from the note's own source:

{{fence repos:essay-surface/notes/format-demo.note "digraph note_to_page"}}

None of the ten types changed shape. The demo pushed back once, on a link to another note by name: the renderer has no way to tell whether that note is a `.md` or a `.note`. So the server answers `/notes/<name>` with a redirect, a route rather than a type change.

### A check for the new path

`scripts/note_check.shale` checks the Roc and runs its tests, builds `render-note`, renders every `.note`, draws each diagram with the page's own viz.js, and posts replies to a real server. Its first run stopped at its first stage, the format check, because the agent had added tests after last running `roc fmt`. Then each plant went red:

{{fence repos:essay-surface/journal/demo.md "plant 1, each end one byte short"}}

Plant 3 is a stray module that nothing imports, with one `expect`. `roc test` said all 60 passed, which is true. So the check counts the `expect` lines in the files and holds the runner to that number:

{{lines repos:essay-surface/scripts/note_check.shale from "local expects = 0" to "want all"}}

Never take a test runner's count as the total. Count the tests in the files and hold the runner to it, or a test that nothing runs passes forever.

The journal calls the commit check by its old name, `fast`, since renamed `check_before_commit.shale`:

{{fence repos:essay-surface/journal/demo.md "fast: PASS"}}

Green again ✅

### A red of our own

The release check, then named `full.shale` and now `check_before_release.shale`, passed every stage but the audit, which stopped on a note named `shale-book-part-two.md`. That note was the orchestrator's own, from that afternoon. The agent ran the audit on a worktree from before its change, saw the same red, and left the audit's list of pages it excuses alone. When a gate goes red on something that isn't yours, prove it on the old tree, and leave the pages the gate excuses alone. The orchestrator then fixed the cause: the note had a bare code fence in a sentence, and the fix was the sentence.

### The release

Two things blocked the release, and the orchestrator cleared both. One was that red audit. The other was the cost the agent had named: a released server would have shown every `.note` as a page that can't be shown. `release.shale` now builds the renderer with the pinned Roc and moves it with its server through every swap:

{{lines repos:essay-surface/scripts/release.shale from "function rel_move_bin(t, from, to)" to "if exists(r .. from) then rel_mv(r .. from, r .. to) end"}}

It was proved on dev first, with a release, a rollback and a roll-forward. Then it went to Steve's own server, with all 17 comments where they were.

### The eye test

Steve opened the demo on his surface. He called the work Session 9:

> Session 9 looks good, to be clear! It's on track.

That evening, after a day on the new surface in place of the old one:

> The dogfooding is working! I barely notice that I'm using an 'in progress' product now.

### The gates now

`note_check.shale` is a stage of the commit check, so every commit checks the Roc, renders every `.note` and draws its diagrams. Every release builds `render-note` and renders the demo note with it before touching anything.
