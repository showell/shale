module [Row]

# A record type on one line whose one field is a function of three bare
# arguments: five tokens, so no stack, and its braces are no function type.
Row : { on_pick : U8, U8, U8 -> U8 } # findings 1
