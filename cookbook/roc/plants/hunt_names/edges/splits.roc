# Edges for `preposition_split`: three prepositions on one stem, a pair of
# opposites beside a third, a `_from` worker beside two others, and the same
# stem in a module type's members.
# keywords: Through, by
rows_in : U64 -> U64

rows_of : U64 -> U64

rows_at : U64 -> U64

cells_before : U64 -> U64

cells_after : U64 -> U64

cells_at : U64 -> U64

marks_in : U64 -> U64

marks_from : U64, U64 -> U64

marks_of : U64 -> U64

steps_through : U64 -> U64

steps_by : U64 -> U64

Grid := [].{
	rows_in : U64 -> U64

	rows_on : U64 -> U64
}
