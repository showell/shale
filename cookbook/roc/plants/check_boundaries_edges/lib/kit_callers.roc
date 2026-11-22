# Edges for check_boundaries: the same first argument at both calls of a
# library's member is no `fixed`, and at both calls of a file's own
# function under `lib/` it is.
call_a = |x| Kit.spaced(4, x)

call_b = |x| Kit.spaced(4, x)

local_fixed = |n, x| n + x

local_a = |x| local_fixed(5, x)

local_b = |x| local_fixed(5, x)
