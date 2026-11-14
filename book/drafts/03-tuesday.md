# Draft

## Tuesday: replies that stay put

Tuesday's prompt asks for the feature the surface exists for:

{{quote repos:essay-surface/journal/prompts/tuesday.md}}

The first bullet is the hard one. The old surface stores a comment's place as a position, `"para_index": 4`, so a paragraph added above sends every comment below it one paragraph off. Today we read the agent's scripts at review speed, too. It proposes to keep some of them, and the question for each is whether it earns its place.

### Arguing from real paragraphs

The obvious design: a comment remembers its paragraph's text, and on each render it goes to the most similar block above some threshold. The agent measured that before building it. A Go test scored every block of every real note against its nearest other block, and against itself after a typo, a changed word or a split:

{{fence repos:essay-surface/journal/tuesday.md "words: nearest other block"}}

Scores are in thousandths. One typo in a short block drops it to 250, while 1% of blocks have a look-alike in the same note at 714 or more, so the two ranges overlap. A simulation of the decision itself made that concrete:

{{fence repos:essay-surface/journal/tuesday.md "trigrams deleted  T=300"}}

At any threshold low enough to keep an edited short block, a deleted block drags its comments onto a look-alike. So the design added structure. A comment also remembers the fingerprints of the blocks before and after it, and goes, in order, to the block with the same text, the block still between the same two neighbors, the most similar block, or the orphans at the foot of the page. A deleted block leaves its neighbors touching, so the second rule tells a deletion from an edit.

The first simulation of the real resolver ran into its timeout, and the stop came on the line that read its exit code:

{{fence repos:essay-surface/journal/tuesday.md "110.0 s in all"}}

A killed program has no `code`; its `ended` says why. The agent treated the slowness as a finding, cached each block's features once a note, and ran it again:

{{fence repos:essay-surface/journal/tuesday.md "precompute features once per note"}}

At scroll speed: it edits one test file, runs `go test` once with `env` pointing it at the real notes, and prints the lines for one threshold.

{{fence repos:essay-surface/journal/tuesday.md "real notes, ended exit, 49158 ms"}}

Across 22,900 real paragraphs, every edit in place kept its comments. The agent chose the word measure at 600 because it misplaced fewer deleted paragraphs' comments, 345 against 635: a wrong placement misleads, and an orphan is honest. That's the day's turn. The design was argued from Steve's own paragraphs, and the comparison is committed as it ran, so anyone can run it again.

### Seeding faults

Sixteen hand-written anchoring tests passed on their first run. A green is a claim, so the agent broke the code twice, on purpose, and put it back each time:

{{fence repos:essay-surface/journal/tuesday.md "plant 1: no in-place rule"}}

We call this a plant; the established name is fault seeding. `edit(path, old, new)` replaces text that must occur exactly once, so each break and each repair lands exactly where it says. Each `run` goes without `must`, since a red is the point. The output names the tests each fault reached:

{{fence repos:essay-surface/journal/tuesday.md "--- FAIL: TestAnchorTypoFixed"}}

At review speed, this script runs once and goes. It proved the tests once, and a fault left in place would prove nothing more. The agent did the same for the front end: `check.shale` now runs `node --check comments.js`, since `go test` never parses it, and one planted syntax error turned that stage red.

### A script worth keeping

`scripts/comments_smoke.shale` runs the whole comment flow through the real server. It posts a comment, fixes a typo and adds a paragraph above it on disk while the server runs, then deletes the paragraph. Its checks go through one small function:

{{function repos:essay-surface/scripts/comments_smoke.shale want}}

The end of the script shows how it holds a server:

{{lines repos:essay-surface/scripts/comments_smoke.shale from "local job = essay_start(" to "for l in result.value do print(l) end"}}

`attempt` runs `flow` and gives back its result or its stop, so the server is stopped before any verdict. That is the review question for every script that starts a server, and `bench.shale` failed it once today. A budget stop is the one stop `attempt` can't catch, and the server stayed up until the agent stopped it by its record. The bench's budget header now leaves room.

{{fence repos:essay-surface/journal/tuesday.md "ok: and the file still has it"}}

### Every check passed

The report listed six things to check from now on. The last was an eye test the agent couldn't run, since the box has no browser. Steve opened a note in his, and found no way to reply. The buttons sat at opacity 0 and appeared only on hover. On the old surface, buttons follow only prose paragraphs, so hover had him hunting for buttons that weren't there. His words: "Hover is an anti-feature."

The fix set the buttons at opacity 0.6. Their first label was "note", which already meant the essays, so a second commit made it "reply". Steve has written his real replies on the new surface ever since. The first landed on a bullet about "a rename pass", and it read back correctly from the comments file. On Thursday, hidden reply buttons become a check that runs without a browser.

### What's a check now

`check.shale` parses the front end and pins the anchoring simulation on the samples, so any change to the anchoring shows its new numbers. The browser caught what every check missed, and that miss is the next check.
