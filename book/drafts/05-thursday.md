# Draft

## Thursday: making the checks fail

{{quote repos:essay-surface/journal/prompts/thursday.md}}

Wednesday ended on `SAFE TO SHIP`. Could that line ever say anything else?

### One fault per stage

The technique is old. Putting a fault in once, to prove a check catches it, is fault seeding, or mutation testing when a machine does it. We call a seeded fault a plant. Steve had never heard the word before he worked with agents, and plants sometimes irritate him: they slow gates, often through needless recompiles. Done right, they catch tests that test nothing, as Wednesday's deaf watch did in chapter 4.

`full.shale` has six stages, and its first, `fast`, has five of its own, so Thursday had eleven plants. Every launch has the same shape:

{{fence repos:essay-surface/journal/thursday.md "plant 1, fast's gofmt stage"}}

The fault goes in, the stage runs exactly as `full.shale` runs it, and the reverse `edit` takes the fault out in the same launch. Then `git diff` must be empty and `fast.shale` must pass before the next plant. Each plant costs a rebuild and one run of its stage, from a few seconds to twenty, once. None was committed.

Every stage went red for its fault. The race detector caught a map read without its lock, and `fast` passed with that fault in. The audit named the four real notes an aligned-table fault broke. The dates stage named one real note that a plausible "show the latest edit" change re-dated. The bench caught a cache kept but unused, at 1,776 µs of CPU a request against a budget of 400. Three stages are the only ones that see their faults: race, bench and the smoke.

### A stop with the fault still in

Plant 6's first launch passed `run` an option without the one it needs, and Shale stopped the script after the fault went in and before the reverse edit. The fault sat in `store.go`. The agent took it out before anything else:

{{fence repos:essay-surface/journal/thursday.md "the last launch stopped after planting"}}

Plants 1 to 5 had been lucky. After that, every plant ran under `shale --undo-on-stop`, which puts back every path a stopped run changed. The next try stopped on the agent's own `print`, and the footer showed the undo doing its job:

{{fence repos:essay-surface/journal/thursday.md "undone: 1 path(s) the run changed put back"}}

### Green with the fault in

The fourth plant wrote a commenter's name to the page without escaping it. The tests checked the comment's text, and nothing checked the name:

{{fence repos:essay-surface/journal/thursday.md "ok  test, 2474 ms"}}

That is a real security hole. A commenter named `<script>...` would run code on every reader's page, and every stage of the full check passed with it. The missing piece was a test, and the agent wrote one that puts markup in every field the page shows:

{{fence repos:essay-surface/journal/thursday.md "func TestEveryCommentFieldEscaped"}}

It passed before the plant, failed with it, and passed after. The smoke's plant found a smaller flaw: its red said a file was missing, and now it names the check that failed.

### Fixtures, and what they cost

Steve asked for fixtures that are cheap: data the check reads, never a build of its own. Three kinds stayed. The audit reads a known-bad page and a known-good one before any real note, in about 2,000 steps and under 5 ms. A plant that blinded the audit stopped it there, before any server started. Wednesday's hidden panic became data for `fast.shale`'s reader of `go test`:

{{lines repos:essay-surface/scripts/fast.shale from "A known-bad go test -v output" to "misreads testdata"}}

That costs 3 ms and 3,184 steps, and a plant that stopped the reader from recognizing a panic tripped it at once. The third kind is the reply-button sheets, below.

The rest proved their stages once and left. A known-bad Go file would turn `gofmt` and `vet` red forever unless excluded, and an exclusion is a blanket. A racy program needs a `-race` build of its own, exactly the cost Steve named. A slow bench fixture needs a server.

### Reply buttons, without a browser

On Tuesday every check passed, and Steve opened a page with no visible way to reply: the buttons sat at `opacity:0` until hovered. `buttons_test.go` works the cascade out by hand, on the CSS the page is served with. It judges each place `comments.js` puts a button, at every width a media query names, light and dark. A button is hidden by `display:none`, `visibility:hidden`, or opacities that multiply out to under 0.5. CSS it can't judge, such as `!important`, stops the test. Its header names its limits: it never runs the script, and it doesn't see clipping or contrast.

Its fixtures are seven small sheets, five known-bad and two known-good, at 0.02 s inside `go test`. One is Tuesday's real sheet, taken from git:

{{fence repos:essay-surface/journal/thursday.md "the known-bad and known-good sheets for the reply-button check"}}

Putting Tuesday's `opacity:0` back turned the test red at 51 and 80rem. The checker got a plant of its own, opacities no longer multiplied, and `faint-article.css` caught it:

{{fence repos:essay-surface/journal/thursday.md "faint-article.css: want"}}

### Steve's two replies

Two replies to Wednesday's report arrived mid-run. The first asked for a defensive check that the comments folder's parent exists. The server already failed there, but by accident. Now it checks first, names the path, and makes nothing:

{{fence repos:essay-surface/journal/thursday.md "made: false"}}

The second said phones are out of scope, and to state that clearly. The README now says so in one sentence, and the by-hand list keeps only the desktop browser. The narrow-window CSS stays: out of scope means unchecked, not removed.

`full.shale` went from 30.9 s to 23.3 s, mostly from a quieter box; Thursday's additions cost tens of milliseconds.

### Now a gate

Every stage of `full.shale` has been seen red once, and its kept fixtures run on every check in milliseconds. A page that hides its reply buttons fails `fast.shale` now, and so does a commenter's name that reaches the page unescaped.
