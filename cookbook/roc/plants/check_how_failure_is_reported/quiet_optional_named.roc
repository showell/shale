# Quiet for errors' `optional_tag`: a value tagged by what it is, its
# absence by why, and a tag that only starts like an optional.
latest : List(U64) -> [Latest(U64), NoMessage]
latest = |l|
	match List.last(l) {
		Ok(x) => Latest(x)
		Err(_) => NoMessage
	}

Open : [NoneChosen, Doc(Str)]
