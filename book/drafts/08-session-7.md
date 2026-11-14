# Draft

## Session 7: links, the expedient way

Session 7 asks for the first feature to come out of daily use:

{{quote repos:essay-surface/journal/prompts/session-7.md}}

Steve asked for this in conversation, and the orchestrator wrote the prompt from his words. A cold agent worked for about eleven minutes, Steve didn't step in, and the orchestrator ran the release.

Steve wanted to click the URLs in his notes, a small request that can still be hard. A renderer that links URLs can get them subtly wrong in a dozen ways: a sentence's period pulled into the link, a parenthesis that belongs to the prose, a URL inside code, a URL that carries markup into the page. Steve called it a can of worms.

### The real URLs first

Before designing anything, the agent counted: 514 real notes held 125 `http://` or `https://` URLs. It printed each one with its context. Most were markdown links already, and the bare ones ended in every way the prompt names:

{{fence repos:essay-surface/journal/session-7.md "metal-lynrummy-com.md:5" from "day-wrap-2026-11-12.md:7" to "roc-web-umbrella.md:44"}}

A parenthesis then a period, bold, a table cell, a placeholder host: each became a test case. Then, before any edit, the agent kept a "before". Copy your outputs aside before any change, so you can compare the after byte for byte, and every difference is one you can see. The audit from Session 1 already fetches every real note's page, so it ran once on the unchanged server, and its pages were copied aside:

{{fence repos:essay-surface/journal/session-7.md "before any change: every real note"}}

### The cases, as fixtures

The cases came before the code, as data the tests read: four files in `testdata/links/`, 62 cases in all. Of those, 16 must link, 15 must not, 22 test punctuation, and 9 are hostile notes. Each case is a bit of markdown and the exact HTML it must become. The parenthesis rule is the subtle one: a closing parenthesis belongs to the URL only when the URL opened it.

{{lines repos:essay-surface/testdata/links/punctuation.txt from "~~ case: in parentheses, then a period" to ")</a>).</p>"}}

The test checks that the folder holds exactly the four files it names, so a fifth can't be skipped. The code is one new function, `bareURL`, and one branch in the renderer's inline pass:

{{lines repos:essay-surface/markdown.go from "case c == 'h' && !inLink:" to "k += len(u)"}}

The new tests passed on their first run, so the agent counted the cases the tests read, and found all 62.

### One seeded fault

The hostile notes need a test that doesn't depend on exact HTML: only the renderer's own tags, every `&` an entity, and each link's href and text the same URL, escaped once. To prove that test watches, the agent took out the href's escaping, once, under `--undo-on-stop`:

{{fence repos:essay-surface/journal/session-7.md "seed once: a bare URL's href"}}

`git_diff` shows the tree came back as it was. The escaping test went red by three of its rules:

{{fence repos:essay-surface/journal/session-7.md "planted: exit 1" from "--- FAIL: TestBareURLsEscaped" to "markdown_test.go:259"}}

The last line is the one that matters. A note that writes `&quot;` in a URL sends it to the href raw, and a browser reads it as a quote mark, which ends the attribute. `&quot;` is a valid entity, so the `&` rule passes it, and only the rule that href and text are one URL catches it. Then `check_before_commit.shale` passed with 50 tests, up from 48, so both new ones ran.

### Every real page, before and after

The after pages came from the same audit. The comparison took two stops. The first called `replace_all`, a builtin Shale doesn't have, and stopped before a line ran. The second ran out of steps:

{{fence repos:essay-surface/journal/session-7.md "the step budget (200000000 steps) ran out"}}

A regex search pays for its whole pattern at every byte it examines, and the comparison searched 7 MB of pages twice. That's the fourth regex stop this book has shown, and the journals hold six. Each time, builtins did the job instead. Like the jobs in Session 6, that's a nuisance coming back.

The third version, built on `split` and `find`, puts each link whose text is its own URL back as bare text:

{{fence repos:essay-surface/journal/session-7.md "by split and find (builtins)" from "function bare(page)" to "return {n = n, plain"}}

With those links taken back to text, every after page must equal its before page, byte for byte. Every one did, so the only change on 514 pages was the new links:

{{fence repos:essay-surface/journal/session-7.md "26 bare URLs linked on 24 pages" from "linked  note" to "26 bare URLs linked"}}

No URL was left as text outside code. A clean report counts only from a finder that has found something, so the agent ran the same finder on the before pages:

{{fence repos:essay-surface/journal/session-7.md "before pages: 26 http" from "before pages" to "before pages"}}

It found exactly the 26, so its zero is a real zero. The report also named 15 URLs written as code, left alone as the prompt said, a few of which Steve may want to click. That call is his.

### A boring release

The full check said `SAFE TO SHIP`. Then the orchestrator ran the release script from Session 5 on Steve's own server, unchanged, and its last lines read like the last release's:

{{fence repos:essay-surface/journal/session-7.md "comments kept: 4 files, 11 comments"}}

All eleven comments were kept and back on their pages, and the rollback now names a real commit, `8218ffc`, where Session 5's said `unknown`, since the server it replaced came from an earlier release. Green again ✅

### A second change to the language

The same day, after this session, the regex nuisance became Shale's second language change. `re_find_all` and `re_lines` became builtins, and a search now passes over the bytes no match can begin with. Over 5.3 MB of markdown, a search for links fell from 80 million steps to 3.9 million, and tests hold both to the results of the Shale they replaced. The work had asked for it six times.

### The gates now

The 62 cases run on every edit, inside `check_before_commit.shale`, and the escaping test has been seen red once. The rest, the audit of real notes and the release, took this feature unchanged. That's what the habits are for: when a request arrives, most of the work is the feature itself.
