# Quiet for every check: names Roc's syntax and builtins call by name
# (`[...]`, `!`, `==`, the encoding protocol), which the author does not
# choose.
Set := [].{
	subscript : Set, U8 -> Bool
	not : Set -> Bool
	is_eq : Set, Set -> Bool
	decode : src, fmt -> (Try(Bool, err), src)
	map! : Set, (U8 => U8) => Set
}
