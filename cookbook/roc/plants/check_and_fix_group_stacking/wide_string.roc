module [
	plain,
	xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,
	width_101,
	interpolated,
	nested,
	crashing,
	multiline,
	pattern,
]

plain = Str.concat("a very long string that runs on and on and on, well past the hundredth column of the line", "line") # findings 1 advice strings
xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx = "— — — — — — — — — — — — — — — — — — — — — — — — — — — — — — — — — — — — " # findings 1 advice strings
width_101 = "yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy" # findings 1 advice strings
interpolated = |name| "a very long string that runs on and on and on, well past the hundredth column of the line, ${name}, and on" # findings 1 advice strings
nested = |f| "${f("a very long string that runs on and on and on, well past the hundredth column of the line")} and on" # findings 1 advice strings
crashing = |x| x ?? crash "a very long string that runs on and on and on, well past the hundredth column of the line" # findings 1 advice strings
multiline =
	\\a very long string that runs on and on and on, well past the hundredth column of the line # findings 1 advice strings
	\\short
pattern = |x|
	match x {
		"a very long string that runs on and on and on, well past the hundredth column of the line" => 1 # findings 1 advice strings
		_ => 2
	}
