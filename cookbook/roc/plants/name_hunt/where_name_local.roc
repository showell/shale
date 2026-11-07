# A plant for name_hunt's `where_name`: a local's annotation has a clause too.
tree_size : U64 -> U64
tree_size = |base| {
	doubled : a -> U64 where [a.size : a -> U64]
	doubled = |tree| 2 * tree.size()
	base + doubled(base)
}
