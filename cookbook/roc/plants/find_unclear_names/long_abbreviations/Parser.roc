## A plant for `abbreviation`'s longer forms: the plural of a short word that
## abbreviates, and the abbreviations past four letters that the
## abbreviations file lists.
Parser := [].{
	Node : { attrs : List(Str), delim : U8 }

	parse_args : List(Str) -> List(Str)
	parse_args = |args| {
		params = args
		elem_count = List.len(params)
		expr_text = "x"
		stmt_kind = "y"
		tmpl_name = "z"
		List.concat(params, [expr_text, stmt_kind, tmpl_name]).drop_first(elem_count)
	}
}
