# A plant the rule over-reports: a list's length read after its write in
# one function, which RC1 writes in place in every build (copy-shapes'
# `record-length-after-set`) though `get` there copies. The rule can't tell
# `len` from `get` without the compiler, so it finds both; a reader judges
# these.
Totals : { items : List(U64), total : U64 }

record_length_after_set : Totals, U64, U64 -> Totals
record_length_after_set = |totals, j, i| { items: totals.items.set(j, i) ?? totals.items, total: totals.total + totals.items.len() } # after_consume

