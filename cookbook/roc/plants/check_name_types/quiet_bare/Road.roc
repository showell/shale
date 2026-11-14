## Fields of two types that are not a retyping left halfway.
Road := [].{
	Pixels : F64

	Typed : {
		from : Distance,
		width : Pixels,
		place : Spot,
		same : Distance,
	}

	Bare : {
		from : F64,
		width : F64,
		place : U8,
		same : Distance,
	}
}
