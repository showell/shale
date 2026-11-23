module [x]

x : a -> U8
	where [a.is_eq : a, a -> Bool, a.to_hash : a, Hasher -> Hasher]
x = |_| 1
