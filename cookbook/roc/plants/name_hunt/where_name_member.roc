# A plant for name_hunt's `where_name`: a member of a module type.
Scenery :: [].{
	tree_height : a -> U64 where [a.height : a -> U64]
	tree_height = |tree| tree.height()
}
