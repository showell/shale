# A plant for find_calls_nested_three_deep: four deep is one finding, at the outermost call.
four : Str -> U64
four = |s| List.len(Str.to_utf8(Str.trim(Str.with_ascii_uppercased(s)))) # depth 4
