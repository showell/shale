# count_roc's plants

A small tree that cookbook/roc/count_roc/check_goldens.shale counts, copied
to a fresh folder with a `zig-out/Built.roc` added (git ignores `zig-out`).
Each file is a case of a rule in cookbook/roc/count_roc.shale's header:

- `app/Main.roc`: `expect` lines, indented by spaces, tabs, a carriage
  return, a vertical tab and a form feed, and lines that only look like one (`expected`, `expect_more`, a
  comment). Its last line has no newline, and still counts.
- `app/Empty.roc`: no lines at all.
- `app/tests/Helper.roc`, `app/gates/Gate.roc`, `app/ThingTests.roc`,
  `app/MathExpects.roc`, `app/thing_test.roc`, `app/ThingSpec.roc`,
  `app/Specs.roc`, `app/specs_platform.roc`: a test module by each of the
  default `tests` literals.
- `app/Speck.roc`: a module whose name only starts like a spec's.
- `app/tutorial/Book.roc`: the module `skip=/tutorial/` leaves out.
- `app/.hidden/Hidden.roc`: a hidden folder, never counted.
- `app/notes.txt`: no Roc, never read.
- `lib/Lib.roc`: a second folder for `in`.
