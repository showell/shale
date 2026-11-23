# An edge for errors' `error_alias`: this file's own `Problem` is a record,
# and a file's own declaration answers before another file's.
Problem : { why : Str }

judged : U64 -> Try(U64, Problem)
