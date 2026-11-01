# A plant for boundaries' `fixed`: a file under `lib/`, but a top-level
# function, no module type's member, is the file's own and no API.
twice : I64, I64 -> I64
twice = |k, x| k * x

four : I64 -> I64
four = |x| twice(2, x)

six : I64 -> I64
six = |x| twice(2, x)
