module [has_key, count_by]

# A where clause's two method constraints are two items, whatever commas
# their own types hold.
has_key : List(k), k -> U8 where [k.is_eq : k, k -> Bool, k.to_hash : k, Hasher -> Hasher]
has_key = |_, _| 1

# A record pattern's `..` is no item: two fields.
count_by = |{ total, step, .. }| total + step
