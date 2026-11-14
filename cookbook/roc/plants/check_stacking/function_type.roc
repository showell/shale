module [apply]

# Three arguments of a function type in parentheses, which no comma
# stacks.
apply : (U8, U8, U8 -> U8) -> U8
apply = |_| 0
