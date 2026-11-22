## A file that `roc check` refuses before any rewrite: its annotation says
## `Str` where its value is a `U64`. A rewrite judged against a failing
## check would prove nothing, so the run stops before it tries one.
BadBase := [].{
	size : List(U8) -> Str
	size = |bytes| List.len(bytes)
}
