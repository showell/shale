# Plants for nest_depth: an operator or a field's access between calls is
# no level, so the calls on either side of it still nest.
counted : Str -> Str
counted = |s| U64.to_str(List.len(Str.to_utf8(s)) + 1) # depth 3

first_word : Str -> Str
first_word = |s| Str.trim(List.first(Str.split_on(s, " ")) ?? "") # depth 3

not_empty : Str -> Str
not_empty = |s| Bool.to_str(!List.is_empty(Str.to_utf8(s))) # depth 3

named : Str -> Str
named = |s| Str.trim(Str.concat(parsed(s).name, "!")) # depth 3

paired : Str -> Str
paired = |s| Str.trim(Str.concat(split_pair(s).0, "!")) # depth 3

parsed = |s| { name: s }
split_pair = |s| (s, s)
