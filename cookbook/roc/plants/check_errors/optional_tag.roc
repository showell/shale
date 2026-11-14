# A plant for errors' `optional_tag`: a type that names an optional.
last_of : List(U64) -> [Some(U64), None]
last_of = |l|
	match List.last(l) {
		Ok(x) => Some(x)
		Err(_) => None
	}
