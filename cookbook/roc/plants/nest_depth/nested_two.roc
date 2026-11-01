# A plant for nest_depth: two nests on one line are two findings, each at
# its own column, marked left to right.
both : Str -> (Str, U64)
both = |s| (Str.trim(Str.trim(Str.trim(s))), List.len(Str.to_utf8(Str.trim(Str.trim(s))))) # depth 3, 4
