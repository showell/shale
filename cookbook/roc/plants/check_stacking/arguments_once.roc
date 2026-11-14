module [mix, another]

# One function, one finding: the annotation's six bare arguments, not
# again its lambda's six on the line below.
mix : U8, U8, U8, U8, U8, U8 -> U8 # findings 1
mix = |a, b, c, d, e, f| a + b + c + d + e + f # findings 0

# A lambda under another name's annotation is its own finding.
other : U8 -> U8
another = |a, b, c, d, e, f| a + b + c + d + e + f # findings 1
