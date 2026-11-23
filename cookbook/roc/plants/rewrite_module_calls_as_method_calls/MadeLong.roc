## A rewrite that pushes a line past 100 columns: the stacked call's
## arguments join one line, which the run reports for the sweep to stack.
## The banner was past 100 before, and the short call turns and stays short,
## so neither is reported.
MadeLong := [].{
	banner : Str
	banner = "a banner that was already wider than one hundred columns before any rewrite came along"

	joined : Str, Str, Str -> Str
	joined = |first_part_of_the_long_text, second_part_of_the_long_text, third_part_of_the_long_text|
		Str.concat(
			first_part_of_the_long_text,
			Str.concat(second_part_of_the_long_text, third_part_of_the_long_text),
		)

	size : List(U8) -> U64
	size = |bytes| List.len(bytes)
}

expect MadeLong.joined("a", "b", "c") == "abc"
expect MadeLong.size([1]) == 1
