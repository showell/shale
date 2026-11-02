# A plant for name_hunt's `taken_word`: a function named only by one of
# Roc's words that gives no such thing (annotated, unannotated, and a
# module type's member), and a value named only by a number.
record : Str, Str -> Str
record = |name, text| Str.concat(name, text)

tag = |name| "<${name}>"

two : List(U64)
two = [1, 2]

Html :: [].{
	tag : Str -> Str
	tag = |name| "<${name}>"

	three : List(Str)
	three = ["a", "b", "c"]
}
