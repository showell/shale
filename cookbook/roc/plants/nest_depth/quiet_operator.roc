# Quiet for nest_depth: an operator between calls two deep is two deep,
# and so is a field's access on a call.
summed : Str, Str -> U64
summed = |a, b| List.len(Str.to_utf8(a)) + List.len(Str.to_utf8(b))

counted : List(U8), List(U8) -> Str
counted = |a, b| U64.to_str(List.len(a) + List.len(b))

trimmed_name : Str -> Str
trimmed_name = |s| Str.trim(parsed(s).name)

parsed = |s| { name: s }
