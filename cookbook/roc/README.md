# The Roc cookbook

Recipes for keeping a Roc tree tidy and judging a change to it, written
in Shale over `run` (`roc`, `objdump`). Like the Zig cookbook's, they are
recipes, not builtins: read one, copy it, change it for the job. Each is
tested to parse (`src/shale_cookbook_test.zig`). Run one from the checkout
it works on, `<shale>` being the shale checkout:

    cd <repo> && shale <shale>/cookbook/roc/roc_commas.shale dir=roc width=100

- [name_hunt.shale](name_hunt.shale): **awkward names, a question each**,
  the Zig hunt's five checks over Roc's top-level annotations and
  definitions (`held_raw!` named as a state). `self_test=1` runs its
  plants (`plants/name_hunt/`).
- [roc_commas.shale](roc_commas.shale): **long lines laid out by `roc fmt`.**
  `roc fmt` never wraps a line, and it joins hand-made breaks back. A
  trailing comma after a record's, call's or lambda's last item makes it
  lay that group out one item a line. This puts one on each long line's
  widest group, runs `roc fmt`, and repeats until nothing changes. It lists
  what it can't reach: strings, long conditions, a function type's bare
  arguments.
- [stack.shale](stack.shale): **three or more, one a line.** Any list,
  record, tuple, call's or lambda's arguments or tag payload of three or
  more items on one line is a finding, and so is a line over 100 columns.
  `write=1` gives each a trailing comma, outermost first, and runs `roc fmt`
  until nothing changes; a second run changes nothing. Strings and comments
  are never read as items. `self_test=1` runs its plants (`plants/stack/`).
- [same_code.shale](same_code.shale): **the same machine code.** Two builds
  of an archive, compared instruction by instruction with Roc's hashed
  procedure names (`roc__p` and 32 hex digits, which move with the source's
  layout) masked. It is the judge of a pure reformat, whose archive bytes
  change though its code doesn't. An archive objdump can't read (wasm32) is
  refused, never passed.
- [tree_patch.shale](tree_patch.shale): **a tree's change as a patch.**
  Every file under a changed copy against the original, as one unified
  patch with `a/` and `b/` paths that `patch -p1` applies from the repo's
  root. Dog Park's new-kind proof (`test/new_kind/sheep.patch`) is rebuilt
  this way whenever the Roc it patches moves.
