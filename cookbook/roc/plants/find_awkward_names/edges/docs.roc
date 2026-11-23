# Edges for `stale_doc_name`: a module beside this file that the glob may
# leave out, `Shelf.shelf_width`, and `Shelf.gone_width`, which it lacks.
# A name in a string is no doc: "`not_here_at_all`".
# A file's name is no member: `Shelf.roc`, `Notes.md`.
# Two on a line, `first_gone` and `second_gone`, and one twice,
# `first_gone`, `first_gone`.
# A call, `shelf_count(a, b)`, and an effect, `shelf_count!()`.
# Unclosed `half_open
# The sentence ends. Zig has it as `zig_only_name`
# across a line, and `after_break_gone` is ours again.
#
# Rust's
# `rust_only_name` is cited from the line above.
# A possessive, the module's `own_name`, and a plural's, the shelves' `shelves_name`.
## In a doc comment, `doc_gone`.
## Its own name, `shelf_count`, and its own module, `docs.shelf_count`.
import Shelf

shelf_count : U64
shelf_count = Shelf.shelf_width
