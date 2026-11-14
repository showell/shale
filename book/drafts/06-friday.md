# Draft

## Friday: shipping should be boring

{{quote repos:essay-surface/journal/prompts/friday.md}}

By Friday the surface was in daily use, with real replies on it, and every release was a chore done by hand. A chore done by hand is done a little differently each time. A release done by a script is the same every time, and that sameness is what we mean by boring.

### A release, in order

The agent's script, `release.shale`, does what the prompt asks, in order. It stops before it touches anything unless the checkout is clean and exactly one server runs with the argv it would start. Then it runs the full check from Thursday and reads its last line:

{{lines repos:essay-surface/scripts/release.shale from "-- 2: the full check." to "full check: SAFE TO SHIP"}}

Both have to hold: the check exits 0, and it says SAFE TO SHIP. Next comes the build. Shale builds nothing itself: `go build` makes the binary, beside the old one, and the script writes down the commit it came from and how long the build took.

Before it stops anything, the script copies the comments into a dated folder and compares the copy file by file. It also fetches the old server's index and every page with comments on it, as a record of where each comment sat. Then it stops the old server, keeps its binary as `.prev`, starts the new one, and checks it. The comment check is strict: each comment must follow the same block of the note as before, and still be in its file.

The swap and the checks run under `attempt`, which catches a failure and hands it back as a value:

{{show repos:essay-surface 92fb48b:scripts/release.shale from "local done = attempt(rel_install_and_check, t, before)" to "ROLLED BACK: running"}}

On a failure, the old binary goes back, starts, and gets the same checks the new one failed. A failed release ends where it started, and says so on its last line.

### A check that talked to the wrong server

The first surprise came before the script ran at all. The trial put an "old" server on the dev port, 9300, and the checks' own servers listened there too. The agent ran the smoke check beside the trial server to see what would happen:

{{fence repos:essay-surface/journal/friday.md "the page: 404"}}

The smoke's server logged "listening", then failed to bind, and the smoke went on to ask the trial server for its page. Every script waited for "listening", and the server said it before the port was its own. Here the check failed. With a different server on the port, a check could have passed against the wrong program. Two fixes followed: `server.go` now binds first and says "listening" after, and the checks moved to port 9310, named once in the prelude.

### The trap, sprung

Monday's journal warned that a run that stops after `job_start` leaves its server running. Friday's first trial hit it:

{{fence repos:essay-surface/journal/friday.md "the step budget (5000000 steps) ran out"}}

`attempt` catches a failure, and a budget stop is past its reach. The run stopped after the old server was down and the new one was up, so the rollback never ran. The trial was left serving a build nobody had checked. On the live server, that would have been a release with no check and no way back but by hand, which is how the agent put the trial back.

The cause was a regex over a 67 KB page, at a step a character; plain `split` and `find` now read the pages. The defense was a step budget twenty times what a release uses. The trap itself stayed in the language until Saturday.

### A release that fails on purpose

The prompt asked for a release that fails, and the agent chose its fault carefully. A fault the full check catches never reaches the swap, so it proves nothing about the rollback. The agent looked for a class of fault no check covers, and found one: the server's defaults. Every check passes `-comments` explicitly. The live server passes none. So the agent seeded one fault, on a throwaway branch in its own worktree, that moves the default comments folder inside the notes folder:

{{fence repos:essay-surface/journal/friday.md "*comments = filepath.Join(filepath.Clean(*dir)"}}

The full check said SAFE TO SHIP. The release caught it on the first comment and rolled back:

{{fence repos:essay-surface/journal/friday.md "FAILED: comment f31eaaa22556"}}

Afterward every comments file matched the data byte for byte, and the bad binary was kept as `.failed`, with its log. That's the case for a release check. The full check tests the code the way the tests start it. The release check tests the server the way it really runs, with its real flags and its real data, and only the second one sees a bad default.

A fourth trial seeded a formatting fault, and the full check stopped it: `nothing touched`, the same server pids before and after. Then the agent removed the plant's branch and worktree, since a seeded fault proves a check once.

### The real release

When it worked on dev, we ran it ourselves, on the live server:

```text
release: /home/steve/essay-dogfood on 127.0.0.1:9301
running: pid 2484343, unknown (built before release.shale)
   == fast: PASS, 3558 ms, cpu 3457 ms
   == race: PASS, 2740 ms, cpu 1677 ms
   == anchoring: PASS, 12281 ms, cpu 11753 ms
   == audit: PASS, 1350 ms, cpu 602 ms
   == dates: PASS, 1698 ms, cpu 351 ms
   == bench: PASS, 4925 ms, cpu 1636 ms
full check: SAFE TO SHIP, 26562 ms
built bin/essay-surface.new: 92fb48b built 2026-11-13T17:51:07Z: Journal: Friday, the final script released once more on dev, 926 ms
comments kept: 3 files, 7 comments, in essay-dogfood/releases/2026-11-13T175107Z/comments, each compared
the old server's index and 3 commented notes kept in essay-dogfood/releases/2026-11-13T175107Z/before
stopped the old server; its log kept
started 92fb48b built 2026-11-13T17:51:07Z: Journal: Friday, the final script released once more on dev
ok: the index, 508 notes, the same as before
ok: a note, 2026-11-01-a-day-of-judges.md, 18882 bytes
ok: day-2026-11-02.md, 200, its 1 comments where they were
ok: essay-surface-wednesday.md, 200, its 5 comments where they were
ok: shale-book-draft-2-plan.md, 200, its 1 comments where they were
ok: all 7 comments from before on their pages and in their files
RELEASED: running 92fb48b on 127.0.0.1:9301; to roll back to unknown: cd /home/steve && shale repos:essay-surface/scripts/release.shale data=essay-dogfood port=9301 rollback=1
```

Read it the way you'd read any gate: the verdicts. Six stages passed, the build took under a second, seven comments were kept and found where they were, and the last line says what runs and how to go back. The old version reads `unknown` because it was built by hand, before the script wrote versions down. The full check is nearly all of the 27 seconds; the swap and its checks fit inside one.

The release was the most boring thing we did all week, and it took the most care to make it so. Boring is what a release looks like when every surprise was found on the dev port first.
