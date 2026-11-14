## Scrutinee.roc's helper, a type with a `from_str`, as roc-chat's
## MessageRef: a text that starts `R`.
Ref :: { text : Str }.{
	from_str : Str -> Try(Ref, [NotAReference])
	from_str = |text| if (text.starts_with("R")) Ok({ text: text }) else Err(NotAReference)

	to_str : Ref -> Str
	to_str = |r| r.text
}

expect Ref.from_str("R7").map_ok(Ref.to_str) == Ok("R7")
