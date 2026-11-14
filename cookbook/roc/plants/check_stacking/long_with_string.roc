module [
	xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,
	with_call,
	interpolated,
	in_comment,
]

xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx = "a very long string that runs on and on and on, well past the hundredth column of the line" # findings 1 advice stack
with_call = Str.concat(xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx, "a string") # findings 1 advice stack
interpolated = "${xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx}a very long string that runs on and on and on, well past the hundredth column of the line" # findings 1 advice stack
# A comment's "strings" are no strings, a very long string that runs on and on and on, well past the hundredth column of the line # findings 1 advice comment
in_comment = 1 # and "so is this one" at the end of a line, a very long string that runs on and on and on, well past the hundredth column of the line # findings 1 advice stack
