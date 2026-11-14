# Draft

## Session 8: types first

Part Two's sessions open with types. The prompt gives the mission, and the agent's first report is a set of Roc types for Steve to reply to, before any code. That's new for us, because Steve's earlier Roc projects were mostly ports, where the old program is the spec. Here there's no old program:

{{quote repos:essay-surface/journal/prompts/session-8.md}}

The orchestrator drafted this prompt from Steve's plan. A cold agent worked for about fifteen minutes, from the prompt's commit at 13:40 to the types' at 13:55. Steve stepped in once, with his reply to the report.

### Which notes are real

The repository's `notes/` holds 12 notes, a sample copied from the old surface, essay-repl-server, a Python server. That one holds 522, up from Session 7's 514, since the notes keep growing. So "my real notes in `notes/`" could mean either. The agent read the 12 as the sample the prompt points at, and counted every feature over all 522, since that's what Steve actually writes, and twelve notes are too few to count by.

### Counting before designing

It wrote `scripts/features.shale`, which walks each note line by line, tracks code fences as the Go renderer does, and counts, for each feature, the notes that use it, how often it appears, and where it first appears:

{{fence repos:essay-surface/journal/session-8.md "paragraph                     522"}}

The counts took several passes. Images first counted as links, which the agent fixed before reading the numbers. A first scan found 126 headings with a style in them, and every one was a Roc doc comment inside a code fence of one note; a rerun that skipped fences found 7 notes. Then the examples corrected two readings. The five "links inside italics" were all subtitles, the italic line under a note's title, so the subtitle gets a type of its own. And the lists nested two deep also matched diff lines, so their count is a ceiling. A hand count found the subtitle in 296 notes.

Nesting checks pushed the survey over its step budget, and a cheap `find` of each pattern's literal before its regex brought it back under, at 19.5 seconds a run. Then, to see examples of six features, the agent launched it six times, 117 seconds in all, where one run that printed all six would have taken 20.

Count your real data before you design a format for it, and keep the counter. The agent kept features.shale because the counts are the argument: a reply that questions one is answered by running it again.

### Types that compile

The prompt asked for types and no functions. It never said they must compile, and the agent wrote them as real Roc anyway: ten modules in `format/`, one type each, beside a `roc-version` file that pins the compiler. A block is one of eleven kinds:

{{show repos:essay-surface ea38b4c:format/Block.roc from "Block := [" to "Language : [NoLanguage, Language(Str)]"}}

A point holds one paragraph and its subpoints, and a subpoint holds no list of its own, so lists nested two deep, in at most 9 notes, have no type. The inline text makes a stronger cut:

{{show repos:essay-surface ea38b4c:format/Text.roc from "Text :: List(Text.Span).{" to "Target : [Note(NoteName), Web(WebAddress)]"}}

A style holds a `Phrase`, words and code with no style of its own, so bold inside italics has no type, and no check has to catch it.

Every compile check said `No errors found` in a few milliseconds, which is fast enough to doubt. So the agent planted two faults under `--undo-on-stop`, each put back before the next:

{{fence repos:essay-surface/journal/session-8.md "plant 1, Tabel in Block"}}

Both went red, so the check from `Note.roc` reads the types of every module it imports. Then the hunts, Shale scripts that read Roc's parse tree for weak names and layouts, found five things on their first pass: three names (two `container` hits, on a list's `items` and `Item`, and a `singular_list` hit, on a table's `header`) and two layout hits (`unstack`, on `Table` and `Widget`). A list's `Item` became a `Point`, a table's `header` became `column_headings`, and the two short records went onto one line each. A numbered list's `first` became `first_number` too, since `first` read as its first point. One edit stopped on a doc line quoted wrong, and nothing in that file was written.

Did compiling matter? It was cheap: about two minutes went to writing and checking the types, and about twelve to reading and counting. And it bought a lot. The plants and the hunts ran on real code, not on a sketch someone would translate later. When you ask for a design, ask for it in a form a tool can check, so the first review is the compiler's.

### The short read

The report was a long note on the surface: each type with what it's for and what it leaves out, a table of what the format drops, a name for the files (`.note`), and three questions. Should Note and Block be nominal or opaque? Should a list point hold one paragraph or several? Does the divider stay? The orchestrator published the report beside a short read of its own, with a recommendation for each question. Steve replied:

> Looks good! The honest truth is that I jumped right to the file with the Roc types. I skimmed everything, especially the prose. The Roc types look solid enough to start building out a quick demo with.

He accepted all three recommendations: Note and Block stay nominal, a list point holds one paragraph, and the divider stays.

That's the shape to aim for. Steve gave his judgment where it counts, in the types, and his decisions in one line, without reading every paragraph. Write the report for a reader who skims: the types must carry the design on their own, and every question must arrive with a recommendation, so the answer can be a yes.

Next, a demo puts these types to work and finds where Roc runs.
