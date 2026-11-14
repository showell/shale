# Draft

## Session 4: making the checks fail

Session 4 asks whether those checks can fail:

{{quote repos:essay-surface/journal/prompts/thursday.md}}

A cold agent worked for about seventeen minutes. Steve stepped in once, with two replies that the orchestrator passed on mid-run.

Can every stage really go red? Session 3 saw `NOT SAFE` once, for one fault. Asking whether a gate can fail is reading it at trust speed, a third speed after scroll and review: what proves the gate is watching?

### One fault per stage

This session seeded one fault in each stage, ran the stage, and took the fault out. A plant done once and removed catches tests that test nothing, as the deaf watch did in Session 3. A fixture is the permanent kind: a small input a check must reject or accept, run on every check. Steve is wary of fixtures, because they can slow gates, often through needless rebuilds.

`full.shale` has six stages, and its first, `fast`, has five of its own, so that made eleven plants. Every launch had the same shape: the fault in (`OLD` becomes `NEW`), the stage run exactly as `full.shale` runs it, the fault out, then proof that the tree is back and `fast.shale` passes:

{{fence repos:essay-surface/journal/thursday.md "plant 1, fast's gofmt stage" from "OLD, NEW)" to "after: exit"}}

Each plant costs a rebuild and one run of its stage, from a few seconds to twenty, once. None was committed.

Every stage went red for its fault, and the faults that got past every other stage show why each stage exists. `fast` passed with a map read without its lock, which only the race detector caught. Only the smoke test runs the real server binary, so its fault was invisible to the Go tests.

### A stop with the fault still in

Plant 6's first launch named a stage with no CPU budget, and Shale stops on that. It stopped the script between the fault going in and coming out, leaving the fault in `store.go`. The agent removed it first:

{{fence repos:essay-surface/journal/thursday.md "the last launch stopped after planting"}}

Plants 1 to 5 had been lucky: a stop in any of them would have left its fault behind too. A script that edits files to test something must put them back on any stop, or one stop leaves a bug in the tree. After that, every plant ran under `shale --undo-on-stop`, which puts back every file a stopped run changed. The next try stopped on a mistake in the agent's own `print`, and Shale's last line shows the undo:

{{fence repos:essay-surface/journal/thursday.md "undone: 1 path(s) the run changed put back"}}

### Green with the fault in

The fourth plant wrote a commenter's name to the page without escaping it. The tests checked the comment's text, and nothing checked the name, so `fast` passed:

{{fence repos:essay-surface/journal/thursday.md "ok  test, 2474 ms" from "ok  test" to "fast: PASS"}}

That is a real security hole: a commenter named `<script>...` would run code on every reader's page, and every stage of the full check passed with it. Test every field that reaches the page, not just the one you thought about, since the field you skipped is the hole. The agent wrote the missing test, which puts markup in every field:

{{fence repos:essay-surface/journal/thursday.md "func TestEveryCommentFieldEscaped"}}

It passed before the plant, failed with it, and passed after: green again ✅.

### Fixtures, and what they cost

Three kinds of fixture stayed, each one data the check reads. The audit reads a known-bad page and a known-good one before any real note, in under 5 ms, and a plant that blinded the audit stopped it there, before any server started. The hidden panic from Session 3 became data for the part of `fast.shale` that reads `go test` output:

{{lines repos:essay-surface/scripts/fast.shale from "A known-bad go test -v output" to "misreads testdata"}}

It costs 3 ms, and a plant that stopped the reader from recognizing a panic tripped it at once. The third kind is the reply-button sheets, below.

The rest of the plants proved their stages once and left. A known-bad Go file would turn `gofmt` and `vet` red forever unless the checks skipped it, and a skip that broad would hide real faults too. A racy program needs a `-race` build of its own, exactly the cost Steve named. A slow fixture for the bench needs a server.

### Reply buttons, without a browser

In Session 2 every check passed, and Steve opened a page with no visible way to reply: the buttons sat at `opacity:0` until hovered. `buttons_test.go` works out the CSS cascade itself, on the stylesheet the page is served with. It checks each place `comments.js` puts a button, at every width a media query names, light and dark. A button counts as hidden under `display:none`, `visibility:hidden`, or opacities that multiply out to under 0.5. It never runs the script, and it misses clipping and contrast.

Its fixtures are seven small sheets, five known-bad and two known-good, at 0.02 s inside `go test`. One is the real sheet from Session 2, taken from git:

{{fence repos:essay-surface/journal/thursday.md "the known-bad and known-good sheets for the reply-button check" from "testdata/buttons/tuesday.css" to "faint-article.css"}}

Putting its `opacity:0` back turned the test red at widths of 51 and 80rem. The checker got a plant of its own, opacities no longer multiplied, and `faint-article.css` caught it:

{{fence repos:essay-surface/journal/thursday.md "faint-article.css: want" from "--- FAIL" to "faint-article.css: want"}}

### Steve's two replies

The two replies answered the report from Session 3. The first was the defensive check from One loop. The server already failed when the comments folder's parent was missing, but by accident; now it checks first, names the path, and makes nothing. The second put phones out of scope. The README says so, and the narrow-window CSS stays: out of scope means unchecked, not removed.

### The gates now

Every stage of `full.shale` has been seen red once, and its kept fixtures run on every check in milliseconds. A page that hides its reply buttons fails `fast.shale` now, and so does a commenter's name that reaches the page unescaped.
