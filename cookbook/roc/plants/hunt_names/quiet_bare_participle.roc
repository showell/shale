# Quiet for every check: a two-part name that holds a participle says what
# its thing is, `speed` is a noun that only ends in `ed`, and a value named
# by a participle is no function.
escaped_text : Str -> Str
escaped_text = |text| text

unnumbered_message : Str -> Str
unnumbered_message = |text| text

is_added : List(Str), Str -> Bool
is_added = |names, name| List.contains(names, name)

speed : U64, U64 -> U64
speed = |meters, seconds| meters // seconds

joined : List(Str)
joined = ["a", "b"]

written = "x"
