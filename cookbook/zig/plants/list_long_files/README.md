# list_long_files' plants

A small folder of repos that cookbook/zig/list_long_files/check_goldens.shale
lists, run from this folder as the recipe runs from the folder that holds
the repos. Each file is a case of the recipe's rules:

- `shale/src/long.zig`: six lines, the longest.
- `shale/src/tie_first.zig`, `shale/tie_second.zig`,
  `shale/no_last_newline.zig`: five lines each, a tie kept in the walk's
  order; the last has no newline after its last line, which still counts.
- `shale/blank_lines.zig`: two empty lines.
- `shale/.hidden/hidden.zig`: a hidden folder, never walked.
- `angry-gopher/empty.zig`: no lines at all.
- `angry-gopher/deep/a/b/four.zig`: four lines, deep in its repo; `over=4`
  leaves it out, since a file must be over the limit.
- `angry-gopher/notes.txt`: no Zig, never counted.
- `elsewhere/outside.zig`: a folder that is no repo the recipe names.
- There is no `zig-ast`: a repo that isn't there adds nothing.
