# A clean plant for `abbreviation` in tags: tags of plain words, and Roc's
# own `Ok` and `Err`.
Link : [Missing, Found(Str)]

resolve : Link -> Try(Str, [TemplateMissing])
resolve = |link| match link {
	Missing => Err(TemplateMissing)
	Found(text) => Ok(text)
}
