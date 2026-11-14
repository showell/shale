## No declaration here holds a float. # F64
Quiet :: [].{

	## A local's annotation is the body's business.
	total : List(U8) -> U8
	total = |_list| {
		half : F64
		half = 0.5
		_ = half
		0
	}

	to_f64_ish : U8 -> Str
	to_f64_ish = |_n| "F64"

	Holder : { f64 : U8, f64ish : U8 }

	unannotated = 1.5
}

expect {
	local : F64
	local = 1.5
	local == 1.5
}
