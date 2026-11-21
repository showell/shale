# count_comments' plants

A small tree that cookbook/zig/count_comments/check_goldens.shale counts.
Each file is a case of a rule in cookbook/zig/count_comments.shale's header:

- `src/plain.zig`: code, plain comments, a comment after code (code), an
  indented one, blank lines, and lines ending in a carriage return.
- `src/docs.zig`: `//!`, `///` and `////` doc lines, an empty `//`, and a
  comment whose words are split by a tab, a vertical tab, a form feed and a
  carriage return.
- `src/loud.zig`: the loud lines (`**`, a `──` banner, a heading of two
  upper-case words) and the lines that only look loud.
- `src/dated.zig`: the dated lines (QUEUE, FEEDBACK, a date, `Steve`, `the
  box`, `CC`) and the near misses a word boundary leaves out.
- `src/empty.zig` and `src/only_comments.zig`: no code, so no ratio.
- `src/nested/deep.zig`: a file below a folder of `src`, with UTF-8 in its
  code.
- `tools/tool.zig`, `build.zig`: the rest of the default `files`; the
  build's last line has no newline.
- `other/x.zig`: a file only `files=` names.
