# Draft

## Session 7: a feature arrives

{{quote repos:essay-surface/journal/prompts/links.md}}

After six sessions the surface was in daily use, and the first request to come out of that use was small: Steve wanted to click the URLs in his notes. Small isn't the same as easy. A renderer that links URLs can get them subtly wrong in a dozen ways: a sentence's period pulled into the link, a parenthesis that belongs to the prose, a URL inside code, a URL that carries markup into the page. Steve called it a can of worms. This session shows what the earlier habits buy when a request like that arrives.

### The real URLs first

Before designing anything, the agent counted: 514 real notes held 125 `http://` or `https://` URLs. It printed each one with its context. Most were markdown links already, and the bare ones ended in every way the prompt names:

{{fence repos:essay-surface/journal/links.md "metal-lynrummy-com.md:5" from "day-wrap-2026-11-12.md:7" to "roc-web-umbrella.md:44"}}

A parenthesis then a period, bold, a table cell, a placeholder host: each became a test case. Then, before any edit, the agent kept a "before". The audit from Session 1 already fetches every real note's page, so it ran once on the unchanged server, and its pages were copied aside:

{{fence repos:essay-surface/journal/links.md "before any change: every real note"}}

`yield()` lets the run hold one page at a time, as Session 1 taught. Every page of the change can now be compared with the page as it was.

### The cases, as fixtures

The cases came before the code, as data the tests read: four files in `testdata/links/`, 62 cases in all. Sixteen must link, fifteen must not, 22 test punctuation, and nine are hostile notes. Each case is a bit of markdown and the exact HTML it must become. The parenthesis rule is the subtle one: a closing parenthesis belongs to the URL only when the URL opened it.

{{lines repos:essay-surface/testdata/links/punctuation.txt from "~~ case: in parentheses, then a period" to ")</a>).</p>"}}

The test checks that the folder holds exactly the four files it names, so a fifth can't be skipped. The code is one new function, `bareURL`, and one branch in the renderer's inline pass:

{{lines repos:essay-surface/markdown.go from "case c == 'h' && !inLink:" to "k += len(u)"}}

The new tests passed on their first run. A green is only a claim until you count what ran, so the agent counted the cases the tests read, and found all 62.

### One seeded fault

The hostile notes need a test that doesn't depend on exact HTML: only the renderer's own tags, every `&` an entity, and each link's href and text the same URL, escaped once. To prove that test watches, the agent took out the href's escaping, once, under `--undo-on-stop`:

{{fence repos:essay-surface/journal/links.md "seed once: a bare URL's href"}}

`git_diff` before and after shows the tree came back as it was. The escaping test went red by three of its rules:

{{fence repos:essay-surface/journal/links.md "planted: exit 1" from "--- FAIL: TestBareURLsEscaped" to "markdown_test.go:259"}}

The last line is the one that matters. A note that writes `&quot;` in a URL sends it to the href raw, and a browser reads it as a quote mark, which ends the attribute. `&quot;` is a valid entity, so the `&` rule passes it, and only the rule that href and text are one URL catches it. Then `fast.shale` passed with 50 tests, up from 48, so both new ones ran.

### Every real page, before and after

The after pages came from the same audit. The comparison took two stops. The first called `replace_all`, a builtin Shale doesn't have, and stopped before a line ran. The second ran out of steps:

{{fence repos:essay-surface/journal/links.md "the step budget (200000000 steps) ran out"}}

That's the lesson of Session 1 again. `re_find_all` is written in Shale, in the prelude, so it spends a step a character, and the comparison read 7 MB of pages twice. A prelude regex has now stopped a survey in Session 1, the release trial in Session 5, and this comparison. Each time, builtins did the job instead. A nuisance that keeps coming back is a gap in the language, as Session 6 showed, and this one makes the case for moving Shale's regex helpers into its host, where a match would cost a step like any builtin.

The third version, built on `split` and `find`, puts each link whose text is its own URL back as bare text:

{{fence repos:essay-surface/journal/links.md "by split and find (builtins)" from "function bare(page)" to "return {n = n, plain"}}

With those links taken back to text, every after page must equal its before page, byte for byte. Every one did, so the only change on 514 pages was the new links:

{{fence repos:essay-surface/journal/links.md "26 bare URLs linked on 24 pages" from "linked  note" to "26 bare URLs linked"}}

No URL was left as text outside code. A finder that reports nothing proves little until it has found something, so the agent ran the same finder on the before pages:

{{fence repos:essay-surface/journal/links.md "before pages: 26 http" from "before pages" to "before pages"}}

It found exactly the 26, so its zero is a real zero. The report also named what the prompt left alone: 15 URLs written as code, a few of which Steve may want to click. That's his call, and it's on the page for him to make.

### A boring release

The full check said `SAFE TO SHIP` in 25.7 s. Then we ran the release script from Session 5 on Steve's own server, unchanged. It took 26.3 s, and its last lines read like the last release's:

```text
full check: SAFE TO SHIP, 24566 ms
comments kept: 4 files, 11 comments, in essay-dogfood/releases/2026-11-14T110736Z/comments, each compared
ok: the index, 514 notes, the same as before
ok: all 11 comments from before on their pages and in their files
RELEASED: running bbd9eae on 127.0.0.1:9301; to roll back to 8218ffc: cd /home/steve && shale repos:essay-surface/scripts/release.shale data=essay-dogfood port=9301 rollback=1
```

Eleven comments kept and found where they were, and a rollback that now names a real commit, since the server it replaced came from an earlier release. Green again ✅

Nothing in this session was new. The fixtures, the seeded fault, the audit of real notes and the release were each built for an earlier problem, and each took this feature without a change. That's what the habits are for: when a request arrives, the work is the feature itself.
