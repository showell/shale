module [Handler, Pick]

# A type alias of a function with three bare arguments: the same rule as an
# annotation's, and no group holds them.
Handler : U8, U8, U8 -> U8 # findings 1

Pick(a) : a, a, a -> a # findings 1
