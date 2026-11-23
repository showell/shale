module [label]

# Four calls deep read inside out, with the inner three inside an
# interpolation, which the count goes on through.
label : List(U8) -> Str
label = |l| Str.concat("n", "${Num.to_str(List.len(List.concat(l, l)))}") # depth 4
