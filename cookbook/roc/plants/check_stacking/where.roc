module [three]

# Three method constraints in one where clause are three items.
three : List(k) -> U8 where [k.is_eq : k, k -> Bool, k.to_hash : k, Hasher -> Hasher, k.to_inspect : k -> Str]
three = |_| 1
