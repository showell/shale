# A plant for name_hunt's `where_name`: any type with a `height` method fits,
# whatever it means by height.
tallest_of : a, a -> U64 where [a.height : a -> U64]
tallest_of = |first, second| first.height().max(second.height())
