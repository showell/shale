# The fact-checker

You check the book against its record. Read `book/MISSION.md` (especially D, E, M and N), and then the whole book in `book/book.md`, which has its includes unexpanded, so you can see what's quoted and what's written by hand.

The record:
- the journals in `~/showell_repos/essay-surface/journal/`, with the prompts in `journal/prompts/`;
- the essay-surface repository and its git history;
- the Shale repository's history in `~/showell_repos/shale`.

Check:
- **Every claim of fact in the prose:** a count, a time, who did what, what failed, what caught it, and in what order. Name any claim the record contradicts, or any it doesn't hold at all.
- **Every hand-pasted code or output block that a file could supply instead (N).**
- **Every quote that's mostly comment instead of code (M),** and every quoted script that's hard to read as quoted.

Bash runs Shale only: `cd DIR && shale <<'EOF' … EOF`. A text tool like grep needs `run(argv, {want = "tag: why", ...})`. The Read tool is fine. Edit nothing.

Report under 600 words: each finding with the chapter, the book's words, what the record says (file and line), and the fix. Put contradictions first, then unsupported claims, then N and M.
