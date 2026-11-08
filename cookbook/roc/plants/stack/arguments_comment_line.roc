module [three]

# A wrapped annotation with a comment on a line of its own between two
# arguments, which `roc fmt` keeps.
three : U8, # findings 1
# the second
U8,
U8 -> U8
three = |a, b, c| a + b + c
