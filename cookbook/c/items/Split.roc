## The splitter: a file of Shale's C split into its top-level items, in
## order. There is no C parser on the box, and none is wanted:
## c/check_discipline.shale holds the C to a shape a line reader can trust,
## and the build fails when a file breaks it, so the dialect is this
## reader's contract (c/docs/style.md, "The top level"):
##
##   - every line at column 0 opens a top-level item or closes one, and
##     everything inside an item is indented;
##   - every function starts at column 0 with `static` (or is `int main(`);
##   - an opening brace ends its line, and a declaration is one a line;
##   - comments are `//`, `/* */` only inside a macro; there are no tabs;
##   - a header is framed by `// <name>: ...`, its guard and `#endif`.
##
## Every line of the file is in exactly one item, or is blank, or is a `//`
## comment line between items. A line that is none of these is an error
## naming the file and its line, never a guess. It is c/build/c_items.shale's
## `c_items`, the Shale original, a second copy of this logic, which the fast
## tier's discipline block splits every C file with, since it builds no Roc.
import Guard
import Head
import Item
import Mask
import Words

Split := [].{

	## The text's lines as Shale's `lines` splits them: at each newline, with
	## no empty last line after a final one.
	lines : Str -> List(List(U8))
	lines = |text| {
		bytes = text.to_utf8()
		if bytes.is_empty() {
			[]
		} else if bytes.last() == Ok('\n') {
			bytes.split_on('\n').drop_last(1)
		} else {
			bytes.split_on('\n')
		}
	}

	## The items of the C file at `path`, its text `text`, in order (Item has
	## their fields); `uses` fills each function's, test's and test list's
	## uses. A line the dialect does not account for is an error, the stop's
	## words: `<path>:<line>: <why>: <the line>`.
	items : { path : Str, text : Str, uses : Bool } -> Try(List(Item), Str)
	items = |{ path, text, uses }| {
		file = { path, lines: Split.lines(text) }
		count = file.lines.len()
		var $items = []
		var $comment_first = 0
		var $comment_last = 0
		var $at = 1
		while $at <= count {
			raw = line_at(file, $at)
			if raw.is_empty() {
				$comment_first = 0
				$at = $at + 1
			} else if raw.starts_with(['/', '/']) {
				if $comment_first == 0 {
					$comment_first = $at
				}
				$comment_last = $at
				$at = $at + 1
			} else if raw.starts_with([' ']) {
				return Err(unaccounted(file, $at, "an indented line outside every item"))
			} else {
				opened = item_at(file, $at, Mask.masked(raw))?
				commented =
					if $comment_first == 0 {
						opened
					} else {
						{ ..opened, comment_first: $comment_first, comment_last: $comment_last }
					}
				$items = $items.append(with_uses(file, commented, uses))
				$comment_first = 0
				$at = commented.last + 1
			}
		}
		if path.ends_with(".h") {
			Guard.framed(path, $items)
		} else {
			Ok($items)
		}
	}

	## The masked text of the file's lines from `first` to `last`, counted from
	## 1, joined by newlines: Shale's `c_masked_text`.
	masked_text : List(List(U8)), { first : U64, last : U64 } -> List(U8)
	masked_text = |file_lines, { first, last }| {
		var $text = []
		var $at = first
		while $at <= last {
			if $at > first {
				$text = $text.append('\n')
			}
			$text = $text.concat(Mask.masked(file_lines.get($at - 1) ?? []))
			$at = $at + 1
		}
		$text
	}
}

## A file being split: its path, for the errors, and its lines.
CFile : { path : Str, lines : List(List(U8)) }

## Line `number` of the file, counted from 1.
line_at : CFile, U64 -> List(U8)
line_at = |file, number| file.lines.get(number - 1) ?? []

## The error for line `number`, which the dialect does not account for: its
## place and why first, then the line.
unaccounted : CFile, U64, Str -> Str
unaccounted = |file, number, why|
	unaccounted_text(file, number, why, Mask.text(line_at(file, number)))

## The error for line `number`, with `shown` in place of the line.
unaccounted_text : CFile, U64, Str, Str -> Str
unaccounted_text = |file, number, why, shown|
	"${file.path}:${number.to_str()}: ${why}: ${shown}"

## The item's uses, when asked for and it is a function, a test or a test
## list with a body. A body on its head's line is what follows its `{`.
with_uses : CFile, Item, Bool -> Item
with_uses = |file, item, wanted| {
	has_body = item.body_first != 0
	is_code = item.kind == Function or item.kind == Test or item.kind == TestList
	if wanted and has_body and is_code {
		text = Split.masked_text(file.lines, { first: item.body_first, last: item.body_last })
		body =
			if item.body_first == item.sig_last and item.kind != TestList {
				match Mask.byte_from(text, '{', 0) {
					Ok(brace) => text.drop_first(brace)
					Err(_) => text
				}
			} else {
				text
			}
		{ ..item, uses: Words.uses(body) }
	} else {
		item
	}
}

## The line that closes the braced item opened at line `from`: the first
## line after it at column 0, which must start with `}`. A blank line, an
## indented one or a `#` directive (which C writes at column 0, as an
## `#ifdef` in a body is) is inside; anything else at column 0 is an error.
brace_close : CFile, U64, Str -> Try(U64, Str)
brace_close = |file, from, what| {
	count = file.lines.len()
	var $at = from + 1
	while $at <= count {
		match line_at(file, $at).first() {
			Ok('}') => return Ok($at)
			Ok(byte) if byte != ' ' and byte != '#' => {
				why = "a line at column 0 inside ${what} opened at line ${from.to_str()}, which only a `}` may close"
				return Err(unaccounted(file, $at, why))
			}
			_ => {}
		}
		$at = $at + 1
	}
	Err(unaccounted(file, from, "${what} that no `}` at column 0 closes"))
}

## The last line of the parenthesized item that opens at line `from`: where
## its masked parentheses balance. A line after the first is indented, or
## starts with the closing `)` at column 0.
paren_close : CFile, U64 -> Try(U64, Str)
paren_close = |file, from| {
	count = file.lines.len()
	var $depth = 0
	var $at = from
	while $at <= count {
		raw = line_at(file, $at)
		if $at > from and !(raw.starts_with([' '])) and !(raw.starts_with([')'])) {
			return Err(unaccounted(file, $at, "a line at column 0 inside the parentheses opened at line ${from.to_str()}"))
		}
		code = Mask.masked(raw)
		opens = code.count_if(|byte| byte == '(').to_i64_wrap()
		closes = code.count_if(|byte| byte == ')').to_i64_wrap()
		$depth = $depth + opens - closes
		if $depth <= 0 {
			return Ok($at)
		}
		$at = $at + 1
	}
	Err(unaccounted(file, from, "parentheses that never close"))
}

## The last line of a head that opens at line `from`: the first whose masked
## code ends with `{`, `;` or `}`. A line after the first is indented.
head_close : CFile, U64 -> Try(U64, Str)
head_close = |file, from| {
	count = file.lines.len()
	var $at = from
	while $at <= count {
		raw = line_at(file, $at)
		if $at > from and !(raw.starts_with([' '])) {
			return Err(unaccounted(file, $at, "a line at column 0 inside the head opened at line ${from.to_str()}"))
		}
		match last_code_byte(raw) {
			Ok(byte) if byte == '{' or byte == ';' or byte == '}' => return Ok($at)
			_ => {}
		}
		$at = $at + 1
	}
	Err(unaccounted(file, from, "a head that no `{` or `;` ends"))
}

## The last byte of a line's masked code, trimmed.
last_code_byte : List(U8) -> Try(U8, [ListWasEmpty])
last_code_byte = |raw| Mask.trimmed(Mask.masked(raw)).last()

## The item that line `at` opens, its masked code `code`.
item_at : CFile, U64, List(U8) -> Try(Item, Str)
item_at = |file, at, code| {
	raw = line_at(file, at)
	opens = |text| raw.starts_with(text.to_utf8())
	if opens("#") {
		hash_item(file, at, code)
	} else if opens("static inline ") {
		static_item(file, at, "static inline")
	} else if opens("static ") {
		static_item(file, at, "static")
	} else if opens("typedef ") or opens("enum ") {
		type_item(file, at, code)
	} else if opens("SLICE_TYPE(") or opens("VEC_DEFINE(") or opens("MAP_DEFINE(") {
		generator_item(file, at, code)
	} else if opens("TEST(") {
		test_item(file, at, code)
	} else if opens("TEST_MAIN(") or opens("TEST_PART(") or opens("TEST_PARTS_MAIN(") {
		test_list_item(file, at, code)
	} else if opens("_Static_assert(") {
		static_assert_item(file, at)
	} else if opens("int main(") {
		main_item(file, at)
	} else {
		Err(unaccounted(file, at, "a line at column 0 that opens no item the dialect knows"))
	}
}

## The `static` or `static inline` item at line `at`: a function, a
## prototype or data.
static_item : CFile, U64, Str -> Try(Item, Str)
static_item = |file, at, storage| {
	last = head_close(file, at)?
	flat = spaces_made_one(Split.masked_text(file.lines, { first: at, last }))
	ending = last_code_byte(line_at(file, last))
	match Head.function_head(flat) {
		Ok({ return_type, name }) => {
			head = { ..Item.new(Prototype, name, { first: at, last }), storage, return_type, sig_last: last }
			match ending {
				Ok(';') => Ok(head)
				Ok('}') => Ok({ ..head, kind: Function, body_first: last, body_last: last })
				_ => {
					close = brace_close(file, last, "the function `${name}`")?
					function = { ..head, kind: Function, last: close }
					if close > last + 1 {
						Ok({ ..function, body_first: last + 1, body_last: close - 1 })
					} else {
						Ok(function)
					}
				}
			}
		}
		Err(_) => {
			name = Head.data_name(flat)
			data = { ..Item.new(Data, name, { first: at, last }), storage, sig_last: last }
			if ending == Ok('{') {
				close = brace_close(file, last, "the data `${name}`")?
				Ok({ ..data, last: close, body_first: last + 1, body_last: close - 1 })
			} else {
				Ok(data)
			}
		}
	}
}

## The text with each run of `\s` made one space.
spaces_made_one : List(U8) -> List(U8)
spaces_made_one = |text|
	text.fold(
		[],
		|out, byte|
			if !(Mask.is_space(byte)) {
				out.append(byte)
			} else if out.last() == Ok(' ') {
				# A space is `\s`, so one that ends `out` is a run's already.
				out
			} else {
				out.append(' ')
			},
	)

## The `typedef` or `enum` item at line `at`.
type_item : CFile, U64, List(U8) -> Try(Item, Str)
type_item = |file, at, code| {
	kind = match Head.type_keyword(code) {
		Struct => Struct
		Enum => Enum
		Union => Union
		NoKeyword => Typedef
	}
	last = head_close(file, at)?
	headed = { ..Item.new(kind, "", { first: at, last }), sig_last: last }
	braced =
		if last_code_byte(line_at(file, last)) == Ok('{') {
			close = brace_close(file, last, "the ${Item.kind_name(kind)}")?
			{ ..headed, last: close, body_first: last + 1, body_last: close - 1 }
		} else {
			headed
		}
	text = Split.masked_text(file.lines, { first: at, last: braced.last })
	declared =
		if code.starts_with("typedef".to_utf8()) {
			match (kind, Head.pointer_name(text, WithoutConst), Head.declared_name(text)) {
				(Typedef, Ok(pointer), _) => pointer
				(_, _, Ok(name)) => name
				_ => ""
			}
		} else {
			Head.enum_tag(code) ?? ""
		}
	enumerators = if kind == Enum {
		Words.enumerators(text)
	} else {
		[]
	}
	name =
		match enumerators.first() {
			Ok(first) if declared == "" => first
			_ => declared
		}
	if name == "" {
		Err(unaccounted(file, at, "a ${Item.kind_name(kind)} whose name this reader can't find"))
	} else {
		Ok({ ..braced, name, enumerators })
	}
}

## The `#` item at line `at`: an include, a macro (to its last `\` line),
## the header's guard (Split.items frames it), or another directive.
hash_item : CFile, U64, List(U8) -> Try(Item, Str)
hash_item = |file, at, code| {
	count = file.lines.len()
	var $last = at
	while line_at(file, $last).last() == Ok('\\') and $last < count {
		$last = $last + 1
	}
	last = $last
	match Head.directive(code) {
		Err(_) => Err(unaccounted(file, at, "a `#` line with no directive"))
		Ok({ word, argument: _ }) if word == "include" =>
			match Head.include_path(line_at(file, at)) {
				Ok(path) => Ok(Item.new(Include, path, { first: at, last }))
				Err(_) => Err(unaccounted(file, at, "an `#include` with no path"))
			}
		Ok({ word, argument }) if word == "define" => {
			defined = Item.new(Macro, argument, { first: at, last })
			if last > at {
				makes = Words.suffixes(Split.masked_text(file.lines, { first: at, last }))
				Ok({ ..defined, body_first: at + 1, body_last: last, makes })
			} else {
				Ok(defined)
			}
		}
		Ok({ word, argument }) => {
			name = if argument == "" {
				word
			} else {
				"${word} ${argument}"
			}
			Ok(Item.new(Directive, name, { first: at, last }))
		}
	}
}

## The `VEC_DEFINE(`, `MAP_DEFINE(` or `SLICE_TYPE(` item at line `at`.
generator_item : CFile, U64, List(U8) -> Try(Item, Str)
generator_item = |file, at, code|
	match Head.generator(code) {
		Err(_) => Err(unaccounted(file, at, "a generator line whose name and type this reader can't find"))
		Ok({ macro, name, held_type }) => {
			close = paren_close(file, at)?
			Ok({ ..Item.new(Generator, name, { first: at, last: close }), macro, held_type })
		}
	}

## The `TEST(name) {` item at line `at`.
test_item : CFile, U64, List(U8) -> Try(Item, Str)
test_item = |file, at, code|
	match Head.test_name(code) {
		Err(_) => Err(unaccounted(file, at, "a `TEST(` line that is not `TEST(name) {`"))
		Ok(name) => {
			close = brace_close(file, at, "the test `${name}`")?
			test = Item.new(Test, name, { first: at, last: close })
			if close > at + 1 {
				Ok({ ..test, body_first: at + 1, body_last: close - 1 })
			} else {
				Ok(test)
			}
		}
	}

## The `TEST_MAIN(`, `TEST_PART(` or `TEST_PARTS_MAIN(` item at line `at`,
## named for its macro, or a `TEST_PART(` for its part.
test_list_item : CFile, U64, List(U8) -> Try(Item, Str)
test_list_item = |file, at, code| {
	head = Head.test_list(code)
	close = paren_close(file, at)?
	name = if head.macro == "TEST_PART" and head.argument != "" {
		head.argument
	} else {
		head.macro
	}
	Ok({ ..Item.new(TestList, name, { first: at, last: close }), body_first: at, body_last: close })
}

## The `_Static_assert(` item at line `at`, named for its first name that is
## not `_Static_assert` or `sizeof`.
static_assert_item : CFile, U64 -> Try(Item, Str)
static_assert_item = |file, at| {
	close = paren_close(file, at)?
	names = Words.names(Split.masked_text(file.lines, { first: at, last: close }))
	name = names.find_first(|each| each != "_Static_assert" and each != "sizeof") ?? ""
	Ok(Item.new(StaticAssert, name, { first: at, last: close }))
}

## The `int main(` function at line `at`.
main_item : CFile, U64 -> Try(Item, Str)
main_item = |file, at| {
	last = head_close(file, at)?
	close = brace_close(file, last, "the function `main`")?
	main = { ..Item.new(Function, "main", { first: at, last: close }), return_type: "int", sig_last: last }
	if close > last + 1 {
		Ok({ ..main, body_first: last + 1, body_last: close - 1 })
	} else {
		Ok(main)
	}
}
