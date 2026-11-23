# Edges for errors' `placeholder`: escapes, a long string, non-ASCII text,
# a string with something interpolated, and a `??` with no lookup.
tabbed = |l, i| List.get(l, i) ?? "a\tb"

quoted = |l, i| List.get(l, i) ?? "say \"no\""

accented = |l, i| List.get(l, i) ?? "café"

dollar = |l, i| List.get(l, i) ?? "$5"

built = |l, i, n| List.get(l, i) ?? "n=${n}"

plain = |t| t ?? "none"

dict = |d, k| Dict.get(d, k) ?? "missing"
