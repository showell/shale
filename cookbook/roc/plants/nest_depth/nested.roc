# Plants for nest_depth: calls three deep, read inside out.
shout : Str -> Str
shout = |s| Str.concat(Str.trim(Str.with_ascii_uppercased(s)), "!") # depth 3

# A method's arguments nest as a call's do.
wrapped : Str -> Str
wrapped = |s| s.concat(Str.trim(Str.with_ascii_uppercased(s))) # depth 3

# Inside a tag's payload, still three calls deep.
tagged : Str -> Try(Str, [Empty])
tagged = |s| Ok(Str.concat(Str.trim(Str.with_ascii_uppercased(s)), "!")) # depth 3
