## Which calls are peer calls (peer_calls.shale). A method call names a
## subject, so a symmetric function, one where no argument is the subject, is
## called through its module: `U64.max(a, b)`, `Str.concat(a, b)`,
## `List.map2(xs, ys, f)`. The symmetric functions are a list of names,
## cookbook/roc/symmetric_functions.txt, and a call of one is a peer call
## when its first two arguments' types have one head, as roc-types writes
## them at the call with each alias read through to what it names (a `Hue :
## U32` is a `U32`). A function off the list (`starts_with`, a unit's `plus`)
## keeps its method call, whatever its arguments.
import Source
import lib.Text

Peers := [].{

	## Whether two arguments are peers, values of one type: `type` is the
	## first's type with its aliases read through, `module` that type's head.
	Verdict : { peers : Bool, type : Str, module_name : Str }

	## A symmetric method call that passes an argument, `x.max(y)`: its node,
	## its receiver's node, its first argument's, the function, and whether
	## every argument has a span, so its text can be moved.
	MethodCall : { node : U64, receiver : U64, first : U64, fn : Str, spanned : Bool }

	## Every alias the files declare (`Hue : U32`, never `:=` or `::`), by the
	## name roc-types gives it, to the text of the type it names. An alias
	## with parameters is left out: reading it through would need its
	## arguments bound. Where two files declare one name, the later file by
	## path wins.
	declared_aliases : List(Source.File) -> Dict(Str, Str)
	declared_aliases = |files| {
		var $out = Dict.empty()
		for file in files.sort_with(|first, second| Text.byte_order(first.path, second.path)) {
			for (node, place) in file.nodes.map_with_index(|node, index| (node, index + 1)) {
				if node.kind == "s-type-decl" and node.kids.len() == 2 {
					header = Source.kid_node(file, node, 1)
					target = Source.kid_node(file, node, 2)
					if is_bare_header(file, header) and header.has_span and target.has_span {
						between = Text.trim(Source.text_between(file, header.to, target.from))
						if between == ":" {
							$out = $out.insert(declared_name(file, place), Source.text_of(file, target))
						}
					}
				}
			}
		}
		$out
	}

	## Whether two arguments, of the types `first` and `second` as roc-types
	## gives them (`""` where it gives none), are peers: both types, their
	## aliases read through, have one head to call through (`List(U64)` and
	## `List(Str)` are peers to `map2`).
	verdict : Str, Str, Dict(Str, Str) -> Peers.Verdict
	verdict = |first, second, aliases|
		if first == "" or second == "" {
			{ peers: Bool.False, type: "", module_name: "" }
		} else {
			resolved = resolved_type(first, aliases, 0)
			module_name = Peers.module_of(resolved)
			peers = module_name != "" and module_name == Peers.module_of(resolved_type(second, aliases, 0))
			{ peers, type: resolved, module_name }
		}

	## The module a value of the type `type_text` takes its methods from, the
	## type's head (`List` for `List(U8)`, `Shelf.Book`), or `""` for a type
	## with none to call through: a type variable, a record, a tuple, a tag
	## union or a function.
	module_of : Str -> Str
	module_of = |type_text| {
		bytes = type_text.to_utf8()
		if Peers.has_top_arrow(bytes) {
			""
		} else {
			length = type_name_length(bytes, 0)
			Str.from_utf8_lossy(bytes.take_first(length))
		}
	}

	## Whether an arrow, `->` or `=>`, stands outside every bracket.
	has_top_arrow : List(U8) -> Bool
	has_top_arrow = |bytes| {
		$depth : I64
		var $depth = 0
		var $place = 0
		while $place + 1 < bytes.len() {
			byte = bytes.get($place) ?? 0
			if byte == '(' or byte == '[' or byte == '{' {
				$depth = $depth + 1
			}
			if byte == ')' or byte == ']' or byte == '}' {
				$depth = $depth - 1
			}
			next = bytes.get($place + 1) ?? 0
			if $depth == 0 and next == '>' and (byte == '-' or byte == '=') {
				return Bool.True
			}
			$place = $place + 1
		}
		Bool.False
	}

	## The symmetric functions in `text`, symmetric_functions.txt: a set of
	## names, one a line, `#` lines its comments; or why a line is no name.
	symmetric_table : Str -> Try(Set(Str), Str)
	symmetric_table = |text| {
		var $names = Set.empty()
		for text_line in Text.lines(text) {
			word = Text.trim(text_line)
			if word != "" and !(word.starts_with("#")) {
				if !(is_function_name(word.to_utf8())) {
					return Err("peer_calls: symmetric_functions.txt: `${word}` is not a function's name")
				}
				$names = $names.insert(word)
			}
		}
		Ok($names)
	}

	## The method calls in the file of a function in `symmetric` that pass an
	## argument.
	method_calls : Source.File, Set(Str) -> List(Peers.MethodCall)
	method_calls = |file, symmetric| {
		var $calls = []
		for (node, place) in file.nodes.map_with_index(|node, index| (node, index + 1)) {
			if node.kind == "e-method-call" and node.kids.len() == 2 {
				receiver = Source.kid_node(file, node, 1)
				if receiver.kind == "receiver" and receiver.kids.len() == 1 {
					args = Source.kid_node(file, node, 2)
					fn = if node.method.starts_with(".") {
						Str.from_utf8_lossy(node.method.to_utf8().drop_first(1))
					} else {
						node.method
					}
					if args.kind == "args" and !(args.kids.is_empty()) and symmetric.contains(fn) {
						value = Source.kid(receiver, 1)
						spanned = Source.node_at(file, value).has_span and args.kids.all(|arg| Source.node_at(file, arg).has_span)
						call = { node: place, receiver: value, first: args.kids.first() ?? 0, fn, spanned }
						$calls = $calls.append(call)
					}
				}
			}
		}
		$calls
	}
}

## Whether a type declaration's header has no parameters.
is_bare_header : Source.File, Source.Node -> Bool
is_bare_header = |file, header|
	header.kind == "header" and (header.kids.is_empty() or Source.kid_node(file, header, 1).kids.is_empty())

## The name roc-types gives the type declared at node `place`: the names of
## the declarations around it and its own, joined by dots (`Peers.Hue` for a
## `Hue` declared in `Peers`'s block).
declared_name : Source.File, U64 -> Str
declared_name = |file, place| {
	var $names = []
	var $at = place
	while $at > 0 {
		node = Source.node_at(file, $at)
		if node.kind == "s-type-decl" {
			$names = $names.prepend(Source.kid_node(file, node, 1).name)
		}
		$at = node.parent
	}
	Str.join_with($names, ".")
}

## The type `type_text` with every alias in it read through, its runs of
## space made one, up to 8 aliases deep.
resolved_type : Str, Dict(Str, Str), U64 -> Str
resolved_type = |type_text, aliases, depth| {
	bytes = type_text.to_utf8()
	var $out = []
	var $changed = Bool.False
	var $place = 0
	while $place < bytes.len() {
		length = type_name_length(bytes, $place)
		if length == 0 {
			$out = $out.append(bytes.get($place) ?? 0)
			$place = $place + 1
		} else {
			name = Str.from_utf8_lossy(bytes.sublist({ start: $place, len: length }))
			match aliases.get(name) {
				Ok(named) => {
					$out = $out.concat(named.to_utf8())
					$changed = Bool.True
				}
				Err(_) => {
					$out = $out.concat(name.to_utf8())
				}
			}
			$place = $place + length
		}
	}
	spaced = Str.from_utf8_lossy(one_space(Text.trim(Str.from_utf8_lossy($out)).to_utf8()))
	if $changed and depth < 8 {
		resolved_type(spaced, aliases, depth + 1)
	} else {
		spaced
	}
}

## The bytes with each run of white space made one space.
one_space : List(U8) -> List(U8)
one_space = |bytes| {
	var $out = []
	var $in_space = Bool.False
	for byte in bytes {
		if is_space(byte) {
			if !($in_space) {
				$out = $out.append(' ')
			}
			$in_space = Bool.True
		} else {
			$out = $out.append(byte)
			$in_space = Bool.False
		}
	}
	$out
}

is_space : U8 -> Bool
is_space = |byte| byte == ' ' or byte == '\t' or byte == '\n' or byte == '\r' or byte == 11 or byte == 12

## The length of the type name at byte `start`, as roc-types writes one
## (`U64`, `List`, `Shelf.Book`): a capital, then letters, digits and `_`,
## and again after each `.` that a capital follows; 0 where none starts.
type_name_length : List(U8), U64 -> U64
type_name_length = |bytes, start| {
	if !(Text.is_capital(bytes.get(start) ?? 0)) {
		return 0
	}
	var $end = word_end(bytes, start + 1)
	while (bytes.get($end) ?? 0) == '.' and Text.is_capital(bytes.get($end + 1) ?? 0) {
		$end = word_end(bytes, $end + 2)
	}
	$end - start
}

## The place past the letters, digits and `_` from `start` on.
word_end : List(U8), U64 -> U64
word_end = |bytes, start| {
	var $end = start
	while is_word_byte(bytes.get($end) ?? 0) {
		$end = $end + 1
	}
	$end
}

is_word_byte : U8 -> Bool
is_word_byte = |byte| Text.is_capital(byte) or Text.is_small_letter(byte) or Text.is_digit(byte) or byte == '_'

## Whether the bytes are a function's name: a small letter, then small
## letters, digits and `_`, and an optional last `!`.
is_function_name : List(U8) -> Bool
is_function_name = |bytes| {
	body = if (bytes.last() ?? 0) == '!' {
		bytes.drop_last(1)
	} else {
		bytes
	}
	match body.first() {
		Ok(first) => Text.is_small_letter(first) and body.all(|byte| Text.is_small_letter(byte) or Text.is_digit(byte) or byte == '_')
		Err(_) => Bool.False
	}
}
