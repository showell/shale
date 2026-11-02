# Quiet for every check: `taken_word` judges only a top-level or member
# name that is one taken word whole. A record's field (`tag`, `record`),
# a function named by a number (`Time.two`, a two-digit field), a name of
# more than one word, a local, and a function that gives the very thing
# Roc's word names (`record : Key -> Api.Record`) are none.
Cell : {
	tag : [Th, Td],
	text : Str,
}

Invite : {
	record : Api.Record,
	hash : Str,
}

Time :: [].{
	two : I64 -> Str
	two = |n| Str.concat("0", n.to_str())
}

Key :: [].{
	record : Key -> Api.Record
	record = |key| Api.from_key(key)
}

message_record : Str -> Str
message_record = |text| text

tag_name : Str
tag_name = "p"

summed : U64 -> U64
summed = |n| {
	one = n + 1
	two = one + 1
	two
}
