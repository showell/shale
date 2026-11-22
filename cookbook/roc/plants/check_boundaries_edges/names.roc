# Edges for check_boundaries' names: questions, an effect, constants by
# annotation alone or by pattern alone, a generic one, and two arguments.
is_small = |n| n < 3

has_room = |n| n > 3

asks = |n| is_small(n) and has_room(n)

reset! = |{}| {}

by_type : {} -> Str
by_type = |_| "x"

by_pattern = |{}| "y"

generic_list : {} -> List(a)
generic_list = |{}| []

two_records : {}, {} -> Str
two_records = |_a, _b| "z"

typed_twice : {} -> Str
typed_twice = |{}| "w"
