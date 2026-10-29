# A plant for errors' `error_alias`: an alias of an alias of tags is still
# one shared list of tags.
Problem : [Torn, WrongRecord]

Refusal : Problem

decoded : Str -> Try(U64, Refusal)
