module [three]

# Three bare arguments that `roc fmt` wraps, its return type stacked: one
# finding, at the annotation's first line, and none for its lambda.
three : U8, # findings 1
U8,
(U8 -> U8) -> Try(
	U8, # the sum, through the function
	[Oops],
)
three = |a, b, c| Ok(c(a + b)) # findings 0
