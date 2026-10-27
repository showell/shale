# A plant for name_hunt's `question`: it asks, but gives a count.
is_full : List U8 -> U64
is_full = |slots| List.len(slots)

# And a Bool named as a state: does it mark, or ask?
sealed : Str -> Bool
sealed = |path| !(Str.is_empty(path))
