## A clean plant for `abbreviation`'s short words: real short words (`Map`,
## `Set`, `sum`), accepted terms (`Html`, `Url`, `Json`, `Ast`, `Utf8`, `xs`),
## a plural of a word (`keys`), an axis's change (`dx`) and one letter, which
## is `short_name`'s to judge.
Map := [].{
	Set : { url : Str, size : U64 }

	Ast : [Leaf(Str), Pair(Str, Str)]

	HtmlPage : { json : Str, utf8 : List(U8) }

	sum_of : List(Set) -> U64
	sum_of = |keys| {
		sum = List.walk(keys, 0, |total, key| total + key.size)
		sum
	}

	spread : List(I64) -> I64
	spread = |xs| {
		dx = List.walk(xs, 0, |most, x| Num.max(most, x))
		dx
	}
}
