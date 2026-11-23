module [portion, proportion, portion_size, Slice]

## How much of the whole a slice takes: its portion of the pie.
Slice : { portion : U64, label : Str }

portion : U64 -> U64
portion = |whole| whole // 4

proportion : U64 -> U64
proportion = |whole| portion(whole) * 2

portion_size = 3

describe : Slice -> Str
describe = |slice| "a portion of ${Num.to_str(portion(slice.portion))}" # portion here is prose

make : U64 -> Slice
make = |n| {
    portion: portion(n),
    label: "portion",
}

total = Shapes.portion(8) + Other.portion(2) + portion_size
