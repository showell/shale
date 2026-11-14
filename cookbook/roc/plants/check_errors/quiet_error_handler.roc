# Quiet for errors' `error_alias`: an alias in an argument is a handler's
# input, and an error left to inference or a type variable names no alias.
Unreadable : [KindUnreadable, NameUnreadable]

fail_if_unreadable! : Try(a, Unreadable) => Try(a, [Failed])

inferred : Str -> Try(U64, _)

passed : Try(U64, e) -> Try(U64, e)
passed = |t| t
