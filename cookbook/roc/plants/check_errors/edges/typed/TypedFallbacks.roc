## Edges for errors' `placeholder` and `number_fallback` under the types: a
## placeholder names its type and the lookup's error, and a number falls back
## after any lookup that fails only past the end.
TypedFallbacks := [].{
	name_of : List(Str), U64 -> Str
	name_of = |names, i| List.get(names, i) ?? "nobody"

	label_of : Dict(Str, Str), Str -> Str
	label_of = |labels, key| Dict.get(labels, key) ?? ""

	share_of : List(F64), U64 -> F64
	share_of = |shares, i| List.get(shares, i) ?? 0.5

	seat : U64 -> Try(U64, [OutOfBounds])
	seat = |i|
		if i < 4 {
			Ok(i)
		} else {
			Err(OutOfBounds)
		}

	seat_or_zero : U64 -> U64
	seat_or_zero = |i| TypedFallbacks.seat(i) ?? 0

	first_or_zero : List(U64) -> U64
	first_or_zero = |l| List.first(l) ?? 0

	empty_name : List(Str) -> Str
	empty_name = |names| List.first(names) ?? "anon"

	summary : List(U64), List(Str) -> Str
	summary = |numbers, names| {
		got = TypedFallbacks.seat_or_zero(0) + TypedFallbacks.first_or_zero(numbers)
		"${got.to_str()}${TypedFallbacks.name_of(names, 0)}${TypedFallbacks.label_of(Dict.empty(), "k")}${TypedFallbacks.empty_name(names)}${TypedFallbacks.share_of([], 0).to_str()}"
	}
}
