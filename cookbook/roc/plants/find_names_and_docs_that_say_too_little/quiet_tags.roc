# A clean plant for `abbreviation` in tags: tags of plain words, and Roc's
# own `Ok` and `Err`, and a tag a module qualifies, its module left to its
# own file (`Bool.False`, `Bool.True` in a pattern).
Link : [Missing, Found(Str)]

resolve : Link -> Try(Str, [TemplateMissing])
resolve = |link| match link {
	Missing => Err(TemplateMissing)
	Found(text) => Ok(text)
}

flag = |text| if text == "" Bool.False else Bool.True

ask = |answer| match answer {
	Bool.True => 1
	_ => 0
}
