## A plant for name_hunt's `preposition_split`: `_onto` is a convention
## here (three stems), but `node_of` beside `node_in` is still one noun
## given two prepositions.
Tree := [].{
	label_onto : List(U8), Str -> List(U8)

	edge_onto : List(U8), Str -> List(U8)

	node_onto : List(U8), Str -> List(U8)

	node_of : Str -> Str

	node_in : Str -> Str

	## `_of` on three stems that give one result, `Str`, but take three
	## types: no convention, so no exemption for `node_of`.
	name_of : U64 -> Str

	text_of : Bool -> Str
}
