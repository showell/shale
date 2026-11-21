# Edges for errors' `number_fallback`: the numbers' kinds, and the lookups
# and fallbacks that are not judged.
fraction = |l, i| List.get(l, i) ?? 1.5

big = |l, i| l.get(i) ?? 1_000

hex = |l, i| List.get(l, i) ?? 0x10

negative = |l, i| List.get(l, i) ?? -1

dict = |d, k| Dict.get(d, k) ?? 0

first = |l| l.first() ?? 0

by_name = |l, i| get(l, i) ?? 0

piped = |l, i| l->List.get(i) ?? 0
