## A top-level item of a file of Shale's C, as Split.items gives it: its
## kind, the name it declares, and its lines. Every field is set, so none is
## missing: a line field is 0 and a text field "" where the item has none.
##
##   kind           what it is (Item.Kind; Item.kind_name prints it);
##   name           the name it declares: a function's, a type's, a macro's,
##                  an include's path, a generator's type, a test's; a
##                  directive's word and first argument (`if defined`); an
##                  anonymous `enum`'s first enumerator;
##   first, last    its lines, from its opening line to its closing one;
##   comment_first, comment_last
##                  the `//` lines directly above it, no blank between;
##   sig_first, sig_last
##                  its head: a function's or a test's lines up to the one
##                  its body's `{` ends, a wrapped prototype's lines; else its
##                  first line alone;
##   body_first, body_last
##                  a function's or a test's body (both its line, when the
##                  body shares the head's line), a braced item's inside;
##   storage        `static inline`, `static` or "";
##   return_type    a function's or a prototype's return type, as written;
##   macro, held_type
##                  a generator's macro (`VEC_DEFINE`) and the type it holds;
##   makes          a generator macro's own functions, its `Name##_<x>(`
##                  suffixes (`make`, `push`);
##   enumerators    an enum's constants, in order;
##   uses           when asked for, the identifiers a function's, a test's or
##                  a test list's body names, sorted, each once.
Item := {
	kind : Item.Kind,
	name : Str,
	first : U64,
	last : U64,
	comment_first : U64,
	comment_last : U64,
	sig_first : U64,
	sig_last : U64,
	body_first : U64,
	body_last : U64,
	storage : Str,
	return_type : Str,
	macro : Str,
	held_type : Str,
	makes : List(Str),
	enumerators : List(Str),
	uses : List(Str),
}.{

	## The kinds of item the dialect has.
	Kind : [
		Function,
		Prototype,
		Data,
		Struct,
		Enum,
		Union,
		Typedef,
		Macro,
		Guard,
		Include,
		Directive,
		Generator,
		Test,
		TestList,
		StaticAssert,
	]

	## An item of a kind and a name over its lines, every other field empty:
	## the caller sets what its kind has.
	new : Item.Kind, Str, { first : U64, last : U64 } -> Item
	new = |kind, name, { first, last }| {
		kind,
		name,
		first,
		last,
		comment_first: 0,
		comment_last: 0,
		sig_first: first,
		sig_last: first,
		body_first: 0,
		body_last: 0,
		storage: "",
		return_type: "",
		macro: "",
		held_type: "",
		makes: [],
		enumerators: [],
		uses: [],
	}

	## A kind's name as the Shale original printed it: `function`,
	## `test_list`, `static_assert`.
	kind_name : Item.Kind -> Str
	kind_name = |kind|
		match kind {
			Function => "function"
			Prototype => "prototype"
			Data => "data"
			Struct => "struct"
			Enum => "enum"
			Union => "union"
			Typedef => "typedef"
			Macro => "macro"
			Guard => "guard"
			Include => "include"
			Directive => "directive"
			Generator => "generator"
			Test => "test"
			TestList => "test_list"
			StaticAssert => "static_assert"
		}
}
