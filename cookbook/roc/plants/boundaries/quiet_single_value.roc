# Quiet for boundaries' `single`: a function passed as a value, once.
shouted : Str -> Str
shouted = |s| Str.concat(s, "!")

all_shouted : List(Str) -> List(Str)
all_shouted = |ls| List.map(ls, shouted)
