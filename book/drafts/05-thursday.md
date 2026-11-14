# Draft

## Thursday: making the checks fail

{{quote repos:essay-surface/journal/prompts/thursday.md}}

Wednesday ended on `SAFE TO SHIP`. Could that line ever say anything else?

### One fault per stage

Thursday seeded one fault in each stage, a plant in our word, ran the stage, and took the fault out. Plants sometimes irritate Steve, because they slow gates, often through needless rebuilds. Done once and removed, they catch tests that test nothing, as Wednesday's deaf watch did.

`full.shale` has six stages, and its first, `fast`, has five of its own, so Thursday had eleven plants. Every launch had the same shape: the fault in (`OLD` becomes `NEW`), the stage run exactly as `full.shale` runs it, the fault out, then proof that the tree is back and `fast.shale` passes:

{{fence repos:essay-surface/journal/thursday.md "plant 1, fast's gofmt stage" from "OLD, NEW)" to "after: exit"}}

Each plant costs a rebuild and one run of its stage, from a few seconds to twenty, once. None was committed.

Every stage went red for its fault. The race detector caught a map read without its lock. The audit named the four real notes that a fault in aligned tables broke. The dates stage named one real note that a plausible "show the latest edit" change re-dated. The bench caught a cache kept but unused, at 1,776 µs of CPU a request against a budget of 400.

Three of those faults got past everything else. `fast` passed with the race in, and with the unused cache. Only the smoke test runs the real server binary, so its fault was invisible to the Go tests. The anchoring, audit and dates faults also tripped `fast`'s unit tests; those three stages earn their place on Steve's real notes.

### A stop with the fault still in

Plant 6's first launch gave `run` a CPU budget but no stage name for its stop to report. Shale stopped the script between the fault going in and coming out, leaving it in `store.go`. The agent removed it first:

{{fence repos:essay-surface/journal/thursday.md "the last launch stopped after planting"}}

Plants 1 to 5 had been lucky: a stop in any of them would have left its fault behind too. After that, every plant ran under `shale --undo-on-stop`, which puts back every file a stopped run changed. The next try stopped on a mistake in the agent's own `print`, and Shale's last line shows the undo:

{{fence repos:essay-surface/journal/thursday.md "undone: 1 path(s) the run changed put back"}}

### Green with the fault in

The fourth plant wrote a commenter's name to the page without escaping it. The tests checked the comment's text, and nothing checked the name, so `fast` passed:

{{fence repos:essay-surface/journal/thursday.md "ok  test, 2474 ms" from "ok  test" to "fast: PASS"}}

That is a real security hole: a commenter named `<script>...` would run code on every reader's page, and every stage of the full check passed with it. The agent wrote the missing test, which puts markup in every field the page shows:

{{fence repos:essay-surface/journal/thursday.md "func TestEveryCommentFieldEscaped"}}

It passed before the plant, failed with it, and passed after: green again ✅. The smoke's plant found a smaller flaw: its red said only that a file was missing, and now it names the check that failed.

### Fixtures, and what they cost

Three kinds of fixture stayed, each one data the check reads. The audit reads a known-bad page and a known-good one before any real note, in under 5 ms, and a plant that blinded the audit stopped it there, before any server started. Wednesday's hidden panic became data for the part of `fast.shale` that reads `go test` output:

{{lines repos:essay-surface/scripts/fast.shale from "A known-bad go test -v output" to "misreads testdata"}}

It costs 3 ms, and a plant that stopped the reader from recognizing a panic tripped it at once. The third kind is the reply-button sheets, below.

The rest of the plants proved their stages once and left. A known-bad Go file would turn `gofmt` and `vet` red forever unless the checks skipped it, and a skip that broad would hide real faults too. A racy program needs a `-race` build of its own, exactly the cost Steve named. A slow fixture for the bench needs a server.

### Reply buttons, without a browser

On Tuesday every check passed, and Steve opened a page with no visible way to reply: the buttons sat at `opacity:0` until hovered. `buttons_test.go` works out the CSS cascade itself, on the stylesheet the page is served with. It judges each place `comments.js` puts a button, at every width a media query names, light and dark. A button counts as hidden under `display:none`, `visibility:hidden`, or opacities that multiply out to under 0.5. CSS it can't judge, such as `!important`, stops the test. It never runs the script, and it misses clipping and contrast.

Its fixtures are seven small sheets, five known-bad and two known-good, at 0.02 s inside `go test`. Tuesday's is the real one, from git:

{{fence repos:essay-surface/journal/thursday.md "the known-bad and known-good sheets for the reply-button check" from "testdata/buttons/tuesday.css" to "faint-article.css"}}

Putting Tuesday's `opacity:0` back turned the test red at widths of 51 and 80rem. The checker got a plant of its own, opacities no longer multiplied, and `faint-article.css` caught it:

{{fence repos:essay-surface/journal/thursday.md "faint-article.css: want" from "--- FAIL" to "faint-article.css: want"}}

### Steve's two replies

Two replies to Wednesday's report arrived mid-run. The first asked for a defensive check that the comments folder's parent exists. The server already failed there, but by accident. Now it checks first, names the path, and makes nothing:

{{fence repos:essay-surface/journal/thursday.md "made: false"}}

The second put phones out of scope. The README says so in one sentence, and the by-hand list keeps only the desktop browser. The narrow-window CSS stays: out of scope means unchecked, not removed.

`full.shale` went from 30.9 s to 23.3 s, mostly from a quieter box; Thursday's additions cost tens of milliseconds.

### Now a gate

Every stage of `full.shale` has been seen red once, and its kept fixtures run on every check in milliseconds. A page that hides its reply buttons fails `fast.shale` now, and so does a commenter's name that reaches the page unescaped.
