## A plant for cookbook/roc/foreign.shale: every house check in one module, with
## real findings beside them.
Style := [].{
	Entry : { pos : U64 }

	chosen : List(Str) -> Str
	chosen = |names| List.first(names) ?? crash "no names"

	sign = |x| if (x > 0) 1 else 2

	shape = |x| if x > 1 {
		x
	} else {
		0
	}

	depth = |x| Num.a(Num.b(Num.c(Num.d(x))))

	stacked = |a, b| { one: a, two: b, three: a + b }

	joined = [
		1,
		2,
	]

	wide_one = call_with_a_very_long_name_indeed(first_argument_with_a_long_name, second_argument_with_a_long_name, 1)

	wide_two = "a string so long that the line it sits on goes well past the hundredth column of the file"

	once = |x| x

	shout = |x| once(x) + 1

	leftover = |x| x

	delay_of = |delays, slot| List.get(delays, slot) ?? 0.0
}

expect Style.sign(1) + Style.shape(1) + Style.depth(1) + Style.shout(1) == 1

expect Style.delay_of([], 0) == Style.stacked(1, 2)
