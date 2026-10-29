# A plant for errors' `error_alias`: a module type's alias of tags, named
# qualified, in an effect's result, inside a tuple.
Accounts := [].{
	Unreadable : [KindUnreadable, NameUnreadable]
}

load! : Str => (Try(Str, Accounts.Unreadable), U64)
