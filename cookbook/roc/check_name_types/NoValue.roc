## The `no_value` check: a constant whose first word is `no`, `none` or
## `unset` and whose type is not a `Bool` or a tag union. An ordinary value
## kept to mean "none" should be a tag. A `List` has its own empty case, and a
## type's own member of that type (`Attention.none : Attention`) is its
## scale's bottom, so neither is judged, and neither is a constant with no
## annotation. An alias of a function type is a function's type (`no_draw :
## Draw`, `Draw : U64 -> List(U64)`), so it is not judged either. A string
## with something in it is words about the case, not a stand-in for it (a
## name of words for no match, say), so only `""` is judged.
import Declarations
import Finding
import Tree

NoValue := [].{

	## Each annotated constant that means none and holds an ordinary value.
	findings : Declarations -> List(Finding)
	findings = |declared| {
		types = declared.types
		names = |kept| Set.from_list(kept.map(|type_declared| type_declared.name))
		tag_unions = names(types.keep_if(|type_declared| type_declared.is_tag_union))
		# An alias of a function type (`Draw : U64 -> List(U64)`) makes a value
		# of it a function, which `is_function` sees only when written out.
		is_function_alias = |type_declared|
			type_declared.is_function_type and !(type_declared.is_nominal)
		function_aliases = names(types.keep_if(is_function_alias))
		kinds = { tag_unions, function_aliases }
		declared.values
			.keep_if(|value| value.annotated and !(value.is_function))
			.keep_if(|value| means_none(value.name) and is_plain(value, kinds))
			.map(no_value)
	}
}

## The first words that mean none.
words_for_none : Set(Str)
words_for_none = Set.from_list(["no", "none", "unset"])

## Whether the name's first word, up to its first `_`, means none.
means_none : Str -> Bool
means_none = |name| {
	first_word = match name.split_first("_") {
		Ok(parts) => parts.before
		Err(_) => name
	}
	words_for_none.contains(first_word)
}

## The names of the tag unions and of the aliases of function types.
TypeKinds : { tag_unions : Set(Str), function_aliases : Set(Str) }

## Whether the value's type is an ordinary one, which a tag should stand for.
is_plain : Declarations.ValueDeclared, TypeKinds -> Bool
is_plain = |value, kinds| {
	written = value.written_type
	named = Tree.last_part(written)
	is_own_bottom = value.owner == named
	is_kept_kind = kinds.tag_unions.contains(named) or kinds.function_aliases.contains(named)
	is_shaped = written == "Bool" or written.starts_with("[") or written.starts_with("List(")
	!(is_shaped or value.has_text or is_own_bottom or is_kept_kind)
}

## The finding of a value that means none.
no_value : Declarations.ValueDeclared -> Finding
no_value = |value| {
	path: value.path,
	line: value.line,
	check: "no_value",
	what: "`${value.name}` means none and is a `${value.written_type}`: keep the case as a tag",
}
