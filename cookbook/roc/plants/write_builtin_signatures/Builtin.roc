## A few of Builtin.roc's declarations, written as it writes them: List's
## `concat` takes peers and `map2` too (`List(a)` and `List(b)` are one shape),
## while `fold`, `walk!` and `get` take apart; `len` has one parameter, so no
## row; U8, nested in Num, is named alone, as roc-types names it; and
## `u8_concat`, outside every type's block, has no row.
Builtin :: [].{
	List :: [ProvidedByCompiler].{
		len : List(item) -> U64
		fold : List(item), state, (state, item -> state) -> state
		concat : List(item), List(item) -> List(item)
		map2 : List(a), List(b), (a, b -> c) -> List(c)
		walk! : List(a), b, (b, a => b) => b
		get : List(a), U64 -> Try(a, [OutOfBounds])
		contains : List(a), a -> Bool
			where [a.is_eq : a, a -> Bool]
	}

	Num :: [].{
		U8 :: [].{
			max : U8, U8 -> U8
			shift_left_by : U8, U8 -> U8
		}
	}
}

u8_concat : List(U8), List(U8) -> List(U8)
