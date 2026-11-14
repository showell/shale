## A module whose job is holding the float.
Unit :: F64.{

	of : F64 -> Unit
	of = |x| Unit.(x)

	to_f64 : Unit -> F64
	to_f64 = |Unit.(x)| x
}
