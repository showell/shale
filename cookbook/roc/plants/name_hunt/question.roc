# A plant for name_hunt's `question`: it asks, but gives a count.
is_full : List U8 -> U64
is_full = |slots| List.len(slots)
