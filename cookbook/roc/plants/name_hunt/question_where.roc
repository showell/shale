# A plant for name_hunt's `question`: it asks but gives a `Str`; the last
# `->` is the where clause's, not the result's.
is_member : List(a), a -> Str where [a.is_eq : a, a -> Bool]
is_member = |_, _| "x"
