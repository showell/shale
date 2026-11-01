## Under `types=1` (root TypedApp, an app, as `roc-types` takes one): a first
## argument whose type is another module's (a list, for `Str`) is left
## untried, the type the reason; one whose type is the module's is turned.
Typed := [].{
	size : List(U8) -> U64
	size = |bytes| List.len(bytes)

	text : List(U8) -> Str
	text = |bytes| Str.from_utf8(bytes) ?? ""

	answer : Str
	answer = text([104, 105])
}

expect Typed.size([1]) == 1
expect Typed.text([97]) == "a"
