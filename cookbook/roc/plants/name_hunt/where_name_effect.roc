# A plant for name_hunt's `where_name`: the `!` is no second word.
load_sizes! : List(a) => List(U64) where [a.load! : a => U64]
load_sizes! = |sources| sources.map!(|source| source.load!())
