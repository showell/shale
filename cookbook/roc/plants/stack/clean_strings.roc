module [
	short,
	zzz,
	two,
]

short = "a string" # findings 0
zzz = "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz" # findings 0
two = Str.concat("a", "b") # findings 0
