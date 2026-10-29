# A plant for errors' `error_alias`: another module's alias, in an
# effect's result, inside a tuple.
load! : Str => (Try(Str, Accounts.Unreadable), U64)
