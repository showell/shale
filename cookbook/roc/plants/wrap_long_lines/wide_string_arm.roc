module [describe, joined]

# The string makes the arm's line wide, and the list pattern beside it would
# leave it as wide, so the pattern stays on one line and the line is counted.
describe : List(U8) -> Str
describe = |bytes|
    match bytes {
        [_, _] => "two bytes, the first and the second, which this sentence describes at considerable length"
        _ => "other"
    }

# A near miss: the call around the string is what runs past the width, so it stacks.
joined : Str, Str -> Str
joined = |first, second| Str.concat("a fairly long string literal that takes room on this line", first.concat(second))
