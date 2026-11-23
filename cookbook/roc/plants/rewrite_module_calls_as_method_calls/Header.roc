## Knock.roc's helper, an effect taking bytes, as roc-chat's
## `Host.read_header!`: a request header's values, by name.
Header := [].{
	read! : List(U8) => List(Str)
	read! = |name| if (name.is_empty()) [] else ["7"]
}
