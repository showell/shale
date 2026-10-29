# Quiet for nest_depth: a lambda's body starts again at 0, and so does a
# record's or a list's part (it is named, or placed), unless `loose=1`.
each : List(Str) -> List(Str)
each = |ls| List.map(ls, |l| Str.trim(Str.with_ascii_uppercased(l)))

parts : Str -> { a : Str }
parts = |s| identity({ a: Str.trim(Str.with_ascii_uppercased(s)) })

listed : Str -> List(Str)
listed = |s| List.reverse([Str.trim(Str.with_ascii_uppercased(s))])

identity = |x| x
