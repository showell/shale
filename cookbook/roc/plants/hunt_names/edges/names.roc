# Edges for hunt_names' name reading: a leading underscore, words of digits,
# a doubled underscore, a one-letter effect, a name ending in a preposition
# alone, a negative question and a callback's effect.
_private_count : Str

corner_2 : List(Point)

a__b : U64 -> U64
a__b = |number| number

x! : U64 => U64

is_not_ready : Bool

no_cache : Bool

of : U64 -> U64

ready_path : U64

map_over! : List(U64), (U64 => U64) => List(U64)

take_from : List(U64), U64 -> List(U64)

take_in : List(U64) -> List(U64)

take_of : List(U64) -> List(U64)

shapes_of_for : U64 -> U64

shape_of_at_in : U64 -> U64

of_at : U64 -> U64

two : _

three = 3

record = |fields| fields

Tag : [Opened, Closed(U64)]

open_all! : List(Tag) => {}

set_mode! : [Opening, Close] => {}

port_status! : U64 => U64

uefi! : {} => {}

block_written! : U64 => {}
