## Each `F64` in a declaration here is a finding, however deep in its type.
Signatures :: [].{

	## A type built on a float.
	Reading :: F64.{}

	scaled : Reading, F64 -> Reading
	scaled = |reading, _factor| reading

	Trip : { length : F64, name : Str }

	Mark : [Lost, Found(F64)]

	pair : (F64, F64)
	pair = (1.0, 2.0)

	readings : List(F64) -> U8
	readings = |_list| 0

	place : { at : { x : F64 } } -> U8
	place = |_where| 0

	apply : (F64 -> U8), U8 -> U8
	apply = |_f, n| n

	wide : Num.F64 -> U8
	wide = |_x| 0
}

Ratio : F64

limit : F64
limit = 1.5
