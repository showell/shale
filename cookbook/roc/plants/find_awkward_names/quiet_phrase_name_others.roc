# Quiet for every check: a preposition that ends a name, a question, a constant
# and an effect are no bare phrase.
trees_in_view : U64 -> List(U64)
trees_in_view = |view| [view]

in_cart : List(U64), U64 -> Bool
in_cart = |cart_items, item| cart_items.contains(item)

on_one_segment : { stops : List(U64) }
on_one_segment = { stops: [] }

at_rest : U64
at_rest = 0

in_view : _

on_canvas : U64 -> _
on_canvas = |point| point + 1

on_each! : List(U64), (U64 => {}) => {}
on_each! = |list, act!| for item in list { act!(item) }
