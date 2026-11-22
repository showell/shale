## The words of an item's masked text: its names, the names its body uses,
## an enum's constants and a generator macro's own functions, each the
## regular expression of the Shale original, done by hand: for the names,
## c/build/c_items.shale's; for a body's uses, the `c_words_used` of
## cookbook/c/items.shale, retired (git history), which the fast tier's
## copy left out, since it wants no uses.
import Mask

Words := [].{

	## The names in the text, in order, each as often as it is there:
	## `\b[A-Za-z_]\w*`, a `\w` run that starts with a letter or `_`.
	names : List(U8) -> List(Str)
	names = |text| {
		var $names = []
		var $at = 0
		while $at < text.len() {
			end = Mask.word_end(text, $at)
			if end > $at {
				if Mask.is_name_start(text.get($at) ?? 0) {
					$names = $names.append(Mask.text(text.sublist({ start: $at, len: end - $at })))
				}
				$at = end
			} else {
				$at = $at + 1
			}
		}
		$names
	}

	## The identifiers the text names, sorted, each once: no keyword, no
	## number, no member after `.` or `->`, and no directive's own word
	## (`ifdef`), though the names a directive tests are uses.
	uses : List(U8) -> List(Str)
	uses = |text| {
		bare = without_directive_words(without_members(text))
		sorted = Words.names(bare)
			.keep_if(|name| !(keywords.contains(name)))
			.sort_with(Words.byte_order)
		sorted.fold(
			[],
			|once, name|
				if once.last() == Ok(name) {
					once
				} else {
					once.append(name)
				},
		)
	}

	## An enum's constants, from its masked text: each name that starts an
	## item of its braces, `^\s*([A-Za-z_]\w*)` in each part between commas.
	enumerators : List(U8) -> List(Str)
	enumerators = |text|
		match Mask.byte_from(text, '{', 0) {
			Err(_) => []
			Ok(open) =>
				match Mask.byte_from(text, '}', open + 1) {
					Err(_) => []
					Ok(close) => {
						inside = text.sublist({ start: open + 1, len: close - open - 1 })
						inside.split_on(',').fold(
							[],
							|found, part|
								match leading_name(part) {
									Ok(name) => found.append(name)
									Err(_) => found
								},
						)
					}
				}
		}

	## A generator macro's own functions, its `Name##_<x>(` suffixes, from its
	## definition's masked text, each once, in order: each match of
	## `static\s[^(]*##_(\w+)\s*\(`.
	suffixes : List(U8) -> List(Str)
	suffixes = |text| {
		var $found = []
		var $at = 0
		while $at < text.len() {
			match suffix_at(text, $at) {
				Ok({ suffix, open }) => {
					if !($found.contains(suffix)) {
						$found = $found.append(suffix)
					}
					$at = open + 1
				}
				Err(_) => {
					$at = $at + 1
				}
			}
		}
		$found
	}

	## Two texts in the order of their bytes, a text before every longer one
	## it starts, as Shale's `sort` puts strings.
	byte_order : Str, Str -> [Before, Same, After]
	byte_order = |first, second| {
		pairs = List.map2(first.to_utf8(), second.to_utf8(), |x, y| (x, y))
		match pairs.find_first(|(x, y)| x != y) {
			Ok((x, y)) =>
				if x < y {
					Before
				} else {
					After
				}
			Err(_) =>
				if first.count_utf8_bytes() < second.count_utf8_bytes() {
					Before
				} else if first.count_utf8_bytes() > second.count_utf8_bytes() {
					After
				} else {
					Same
				}
		}
	}
}

## C's keywords and the words of its types and storage, which are no use of
## a name.
keywords : List(Str)
keywords = [
	"auto",
	"break",
	"case",
	"char",
	"const",
	"continue",
	"default",
	"do",
	"double",
	"else",
	"enum",
	"extern",
	"float",
	"for",
	"goto",
	"if",
	"inline",
	"int",
	"long",
	"register",
	"restrict",
	"return",
	"short",
	"signed",
	"sizeof",
	"static",
	"struct",
	"switch",
	"typedef",
	"union",
	"unsigned",
	"void",
	"volatile",
	"while",
	"_Alignof",
	"_Bool",
	"_Static_assert",
	"bool",
	"true",
	"false",
	"NULL",
]

## The text with each member after `.` or `->` made one space:
## `(\.|->)\s*[A-Za-z_]\w*`.
without_members : List(U8) -> List(U8)
without_members = |text| {
	var $out = []
	var $at = 0
	while $at < text.len() {
		byte = text.get($at) ?? 0
		after = if byte == '.' {
			$at + 1
		} else if byte == '-' and text.get($at + 1) == Ok('>') {
			$at + 2
		} else {
			0
		}
		start = Mask.spaces_end(text, after)
		if after > 0 and Mask.is_name_start(text.get(start) ?? 0) {
			$out = $out.append(' ')
			$at = Mask.word_end(text, start)
		} else {
			$out = $out.append(byte)
			$at = $at + 1
		}
	}
	$out
}

## The text with each directive's `#` and word gone, at the start or after a
## newline, the newline kept: `(^|\n)#\s*\w+`.
without_directive_words : List(U8) -> List(U8)
without_directive_words = |text| {
	var $out = []
	var $at = 0
	while $at < text.len() {
		byte = text.get($at) ?? 0
		match directive_word_end(text, $at) {
			Ok(end) => {
				$at = end
			}
			Err(_) =>
				if byte == '\n' {
					match directive_word_end(text, $at + 1) {
						Ok(end) => {
							$out = $out.append('\n')
							$at = end
						}
						Err(_) => {
							$out = $out.append(byte)
							$at = $at + 1
						}
					}
				} else {
					$out = $out.append(byte)
					$at = $at + 1
				}
		}
	}
	$out
}

## Where `#\s*\w+` at `at` ends, at the text's start or after a newline; the
## start alone is asked of at 0.
directive_word_end : List(U8), U64 -> Try(U64, [NoDirective])
directive_word_end = |text, at| {
	is_line_start = at == 0 or text.get(at - 1) == Ok('\n')
	word_start = Mask.spaces_end(text, at + 1)
	word_end = Mask.word_end(text, word_start)
	if is_line_start and text.get(at) == Ok('#') and word_end > word_start {
		Ok(word_end)
	} else {
		Err(NoDirective)
	}
}

## The name an enum's part starts with, past any spaces.
leading_name : List(U8) -> Try(Str, [NoName])
leading_name = |part| {
	start = Mask.spaces_end(part, 0)
	end = Mask.word_end(part, start)
	if end > start and Mask.is_name_start(part.get(start) ?? 0) {
		Ok(Mask.text(part.sublist({ start, len: end - start })))
	} else {
		Err(NoName)
	}
}

## The suffix of `static\s[^(]*##_(\w+)\s*\(` at `at`, and its `(`: the last
## `##_` before the first `(` after `static`, which only a name and spaces
## may follow up to that `(`.
suffix_at : List(U8), U64 -> Try({ suffix : Str, open : U64 }, [NoSuffix])
suffix_at = |text, at| {
	after = at + 7
	if !(Mask.holds_at(text, at, "static")) or !(Mask.is_space_at(text, at + 6)) {
		return Err(NoSuffix)
	}
	match Mask.byte_from(text, '(', after) {
		Err(_) => Err(NoSuffix)
		Ok(open) => {
			span = text.sublist({ start: after, len: open - after })
			match last_paste(span) {
				Err(_) => Err(NoSuffix)
				Ok(paste) => {
					start = after + paste + 3
					end = Mask.word_end(text, start)
					if end > start and Mask.spaces_end(text, end) == open {
						Ok({ suffix: Mask.text(text.sublist({ start, len: end - start })), open })
					} else {
						Err(NoSuffix)
					}
				}
			}
		}
	}
}

## The place of the last `##_` in the bytes.
last_paste : List(U8) -> Try(U64, [Missing])
last_paste = |bytes| {
	var $at = bytes.len()
	while $at >= 3 {
		$at = $at - 1
		if Mask.holds_at(bytes, $at - 2, "##_") {
			return Ok($at - 2)
		}
	}
	Err(Missing)
}
