## The root the typed plants are judged on, for `restored_refcounted`:
## `?? xs` on a list of strings copies on every write in the dev and boxy
## builds (copy-shapes' `str-list-restored`), where `?? crash` and `?? xs` on
## a list of numbers write in place.
app [main] { pf: platform "./QuietPf.roc" }

str_list_restored : List(Str), U64 -> List(Str)
str_list_restored = |xs, j| xs.set(j, "x") ?? xs # restored_refcounted

str_list_or_crash : List(Str), U64 -> List(Str)
str_list_or_crash = |xs, j| xs.set(j, "x") ?? crash "str_list_or_crash: in range"

numbers_restored : List(U64), U64 -> List(U64)
numbers_restored = |xs, j| xs.set(j, 1) ?? xs

main : Str
main = "${str_list_restored(["a"], 0).len().to_str()}${str_list_or_crash(["a"], 0).len().to_str()}${numbers_restored([1], 0).len().to_str()}"
