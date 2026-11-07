# Quiet for every check: two words say what a method is for, and the methods Roc
# calls by name have a fixed meaning.
scenery_size : a -> U64 where [a.scenery_height : a -> U64]
scenery_size = |thing| thing.scenery_height()

is_same_size : a, a -> Bool where [a.is_eq : a, a -> Bool]
is_same_size = |first, second| first == second

total_size : a, a -> a where [a.plus : a, a -> a]
total_size = |first, second| first + second

smaller_size : a, a -> a where [a.minus : a, a -> a, a.negate : a -> a]
smaller_size = |first, second| first - second

is_smaller_size : a, a -> Bool where [a.is_lt : a, a -> Bool]
is_smaller_size = |first, second| first < second

opposite_flag : a -> a where [a.not : a -> a]
opposite_flag = |flag| !flag

doubled_sizes : a -> a where [a.map : a, (U64 -> U64) -> a]
doubled_sizes = |sizes| sizes.map(|size| size * 2)
