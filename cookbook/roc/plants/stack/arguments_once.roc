module [mix, another]

# One function, one finding: the annotation's three bare arguments, not
# again its lambda's three on the line below.
mix : U8, U8, U8 -> U8 # findings 1
mix = |a, b, c| a + b + c # findings 0

# A lambda under another name's annotation is its own finding.
other : U8 -> U8
another = |a, b, c| a + b + c # findings 1
