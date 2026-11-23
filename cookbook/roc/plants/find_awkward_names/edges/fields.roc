# Edges for `container`, `short_name` and `singular_list` on types and
# fields: a doc above that says whose, in a plural or a `y` word; a field
# beside coordinates; a local's annotation, whose fields are not read.

## A shelf's entries.
Entries : { items : List(Str), entry : U64, data : List(U8) }

## The library's registry.
Registries : List(Str)

Info : { x : U64, y : U64, w : U64, h : U64, k : Str }

Color : { r : U8, g : U8, b : U8, a : U8 }

Corner : { a : U64, b : U64 }

Holder : { value : Str, values : List(Str), item_list : List(Str), item_of : List(Str) -> List(Str) }

## A step's doc,
## on two lines.
Value : List(Str)

total : U64 -> U64
total = |number| {
	inner : { data : List(Str) }
	inner = { data: [] }
	number + inner.data.len()
}
