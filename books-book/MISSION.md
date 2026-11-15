# The books book: its mission

This is the one home for the book's rules. Every writer and reader of the book, human or agent, reads it first. Each rule says why, and quotes Steve, the editor, with the date he gave it.

## What the book is

**A. A how-to manual.** The book shows a reader with Claude Code and a GitHub account how the Safari Ride book was built, so they can build one the same way. The voice is plain, clear, practical American English: a manual, with no voice to hunt for.

**B. The order: the reader, what the system does, setup, then machinery and code.** The book opens with who it's for and what it requires. Next it describes what the system does for any book, with each feature's limits, so a reader who wants a different kind of book from Safari's knows what they get. Then setup, as a manual does it: what you tell your agent first, which README to point it at, which repos it will fetch and why, what it installs, and how you'll know setup worked. Machinery and code come last, and a Shale script or an include appears only once the reader has the book building. Steve, on the first draft (2026-11-15): "the first chapter departs from the how-to-manual voice too early. Very early instructions should be about pointing your agent at the README file, having them understand how many repos Claude will fetch, etc. Telling them an example Shale script is premature." Steve, on the second draft (2026-11-15): "have an agent review it for unspoken assumptions. For example, we seem to be assuming that the reader wants to write a book that contains a bunch of code samples. We point out the Roc book in the introduction, but we can't assume they're trying to build a clone of that book. We need to describe the features of our book generation system even before setup."

## The gates

**C. At most 900 words of prose a page.** `build.shale` counts every word outside the code, and stops on more.

**D. Say what happens.** The cookbook's `prose/find_negatives.shale` runs over `book.md` on every build. A negative sentence is rewritten, and `negatives.txt` lists any kept one with its reason. Rewrite rather than keep.

**E. Every quote comes from its file at build time.** Code and quoted rules arrive by include (`{{file}}`, `{{function}}`, `{{lines}}`, `{{runs}}`, `{{stops}}`), never pasted, so a quote can't drift from what it quotes.

**F. Only public resources.** The book names a repo, a path or a page only after checking that it exists and is public, and it never names private infrastructure.
