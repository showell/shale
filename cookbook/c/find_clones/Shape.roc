## A function's counted lines, as the clone finder compares them. A line
## counts when it does something: a blank line, a comment and a line of
## brackets alone (`}`, `} else {`, `default:`) are passed by. Each counted
## line is kept twice: as written (`exact`, to where its code ends, its
## spaces made one) and as its shape (`shape`, every name `I`, every number
## `N`, every string `S` and every character literal `C`, a keyword itself).
import items.Item
import items.Mask

Shape := [].{

	## A counted line: its number, its text as written and its shape.
	CountedLine : { line : U64, exact : Str, shape : Str }

	## A function read for clones: where it is, its name and its counted
	## lines.
	Function : {
		path : Str,
		first : U64,
		last : U64,
		name : Str,
		lines : List(Shape.CountedLine),
	}

	## The function the item is, of the file at `path` whose lines are
	## `lines`, its body's lines counted.
	of_item : Str, List(List(U8)), Item -> Shape.Function
	of_item = |path, lines, item| {
		body = lines.sublist({ start: item.body_first - 1, len: item.body_last + 1 - item.body_first })
		var $counted = []
		var $number = item.body_first
		for raw in body {
			masked = Mask.masked(raw)
			code = Mask.trimmed(masked)
			if !(code.is_empty()) and !(is_brackets(code)) {
				$counted = $counted.append({ line: $number, exact: exact_text(raw, masked), shape: shape_of(code) })
			}
			$number = $number + 1
		}
		{ path, first: item.first, last: item.last, name: item.name, lines: $counted }
	}
}

## C's keywords and the words of its types and storage, which stay
## themselves in a shape.
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

## A line of brackets alone, or a `default:`: no work of its own,
## `^[{}()\[\];, ]*$|^\} else \{$|^default:$`.
is_brackets : List(U8) -> Bool
is_brackets = |code| {
	is_bracket = |byte| byte == '{' or byte == '}' or byte == '(' or byte == ')' or byte == '[' or byte == ']' or byte == ';' or byte == ',' or byte == ' '
	code.all(is_bracket) or code == "} else {".to_utf8() or code == "default:".to_utf8()
}

## The raw line to where its masked code ends (a comment after it cut),
## trimmed, each run of `\s` in it made one space.
exact_text : List(U8), List(U8) -> Str
exact_text = |raw, masked| {
	code_end = match masked.find_last_index(|byte| !(Mask.is_space(byte))) {
		Ok(last) => last + 1
		Err(_) => 0
	}
	kept = Mask.trimmed(raw.take_first(code_end))
	one_spaced = kept.fold(
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
	Mask.text(one_spaced)
}

## A byte that is a token of its own, as text: itself when it is ASCII,
## else its value, `x` and two hex digits, so no two bytes share a token.
byte_token : U8 -> Str
byte_token = |byte|
	if byte < 0x80 {
		Mask.text([byte])
	} else {
		digits = "0123456789abcdef".to_utf8()
		high = digits.get((byte // 16).to_u64()) ?? '0'
		low = digits.get((byte % 16).to_u64()) ?? '0'
		Mask.text(['x', high, low])
	}

## A masked line's tokens with every name `I`, number `N`, string `S` and
## character literal `C`, a keyword itself, joined by spaces. A token is
## `[A-Za-z_]\w*|\d\w*|"[^"]*"|'[^']*'|\S`, leftmost first.
shape_of : List(U8) -> Str
shape_of = |code| {
	var $tokens = []
	var $at = 0
	while $at < code.len() {
		byte = code.get($at) ?? 0
		if Mask.is_name_start(byte) {
			end = Mask.word_end(code, $at)
			name = Mask.text(code.sublist({ start: $at, len: end - $at }))
			$tokens = $tokens.append(
				if keywords.contains(name) {
					name
				} else {
					"I"
				},
			)
			$at = end
		} else if byte >= '0' and byte <= '9' {
			$tokens = $tokens.append("N")
			$at = Mask.word_end(code, $at)
		} else if byte == '"' or byte == '\'' {
			$tokens = $tokens.append(
				if byte == '"' {
					"S"
				} else {
					"C"
				},
			)
			$at = match Mask.byte_from(code, byte, $at + 1) {
				Ok(close) => close + 1
				Err(_) => $at + 1
			}
		} else if Mask.is_space(byte) {
			$at = $at + 1
		} else {
			$tokens = $tokens.append(byte_token(byte))
			$at = $at + 1
		}
	}
	Str.join_with($tokens, " ")
}
