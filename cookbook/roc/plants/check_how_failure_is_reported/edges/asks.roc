# Edges for errors' `error_alias`, the aliases named from Access.roc and
# from outside the files checked.
ask : Str -> Try(U64, Access.Answer)

noted : Str -> Try(U64, Access.Note)

# Declared nowhere here: named in the summary, with how often.
fetch! : Str => Try(Str, Elsewhere.Problem)

fetch_again! : Str => Try(Str, Elsewhere.Problem)

gone : U64 -> Try(U64, Gone)
