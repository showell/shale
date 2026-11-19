# A plant for `abbreviation` in tags: a union's bare tag, a tag with a
# payload, and a tag said only in an expression and a pattern.
Link : [NoRef, Ref(Str)]

resolve : Link -> Try(Str, [TmplMissing])
resolve = |link| match link {
	NoRef => Err(TmplMissing)
	Ref(text) => Ok(text)
}

check = |text| if text == "" Err(BadAttr) else Ok(text)
