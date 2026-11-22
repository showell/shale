## What a Roc file's header says about who chooses its names: a name an app's platform asks for, a
## hook a platform hands its host and a method Roc calls by name were chosen
## somewhere else, so the recipes judge them differently from the names a
## program picks for itself; and which modules a package keeps to itself.
import Bytes
import Text
import Tree

Headers := [].{

	## The methods Roc calls by name, from Roc's docs/langref/static-dispatch.md
	## and operators.md, with the encoders' `encode` and `decode`: a module
	## that declares one has it because Roc asks, not as a name of its own.
	language_fixed_names : Set(Str)
	language_fixed_names = Set.from_list([
		"to_inspect",
		"is_eq",
		"to_hash",
		"plus",
		"minus",
		"times",
		"div_by",
		"div_trunc_by",
		"rem_by",
		"is_lt",
		"is_lte",
		"is_gt",
		"is_gte",
		"range_exclusive_to",
		"range_inclusive_to",
		"range_exclusive_from",
		"range_inclusive_from",
		"range_iter",
		"range_len_if_known",
		"negate",
		"not",
		"from_numeral",
		"from_quote",
		"from_interpolation",
		"iter",
		"next",
		"parser_for",
		"encoder_for",
		"map",
		"map!",
		"subscript",
		"encode",
		"decode",
	])

	## What a folder's `main.roc` exposes, from the file's text (reading the
	## file is the recipe's): `is_package` when its header is `package` or `platform`, and
	## the modules it exposes.
	exposed_by : Str -> { is_package : Bool, modules : List(Str) }
	exposed_by = |text| {
		code = code_of(text)
		word = Headers.header_word(code)
		if word == "package" {
			{ is_package: Bool.True, modules: bracketed_names(code) }
		} else if word == "platform" {
			listed = Text.between(code, "exposes", "]") ?? ""
			{ is_package: Bool.True, modules: bracketed_names("${listed}]") }
		} else {
			{ is_package: Bool.False, modules: [] }
		}
	}

	## The first word of a file's code, its comment lines left out: `app`,
	## `package`, `platform`, `module`, `hosted`, or for a type module the
	## type's name; `""` when it has none.
	header_word : Str -> Str
	header_word = |text| {
		bytes = code_of(text).to_utf8()
		is_word = |byte| Bytes.is_letter(byte) or byte == '_'
		match bytes.find_first_index(is_word) {
			Ok(start) => {
				after = bytes.drop_first(start)
				length = after.find_first_index(|byte| !(is_word(byte))) ?? after.len()
				Str.from_utf8_lossy(after.take_first(length))
			}
			Err(_) => ""
		}
	}

	## The value names a file's tree's header speaks for, in the tree's order:
	## `dictated`, what an app provides,
	## what a platform requires of its app and what it provides to its host;
	## `exposed`, what a `module` or `hosted` header exposes.
	header_names : List(Tree.Node) -> { dictated : List(Str), exposed : List(Str) }
	header_names = |nodes| {
		var $dictated = []
		var $exposed = []
		var $place = 2
		while $place <= nodes.len() and Tree.node_at(nodes, $place).kind != "statements" {
			node = Tree.node_at(nodes, $place)
			if node.kind == "entrypoint" {
				$dictated = $dictated.append(Tree.attribute(node, "value"))
			}
			if node.kind == "symbol-map-entry" {
				$dictated = $dictated.append(Tree.attribute(node, "func"))
			}
			if node.kind == "text" and node.parent > 0 {
				parent = Tree.node_at(nodes, node.parent)
				match Tree.find_attribute(node, "value") {
					Ok(value) if parent.kind == "exposed-lower-ident" =>
						if parent.parent > 0 and Tree.node_at(
							nodes,
							parent.parent,
						).kind == "provides" {
							$dictated = $dictated.append(value)
						} else {
							$exposed = $exposed.append(value)
						}
					_ => {}
				}
			}
			$place = $place + 1
		}
		{ dictated: $dictated, exposed: $exposed }
	}
}

## A file's text with its comment lines left out.
code_of : Str -> Str
code_of = |text| Str.join_with(Text.lines(text).drop_if(|line| Text.trim(line).starts_with("#")), "\n")

## The names in the list between the first `[` and the next `]` of `text`.
bracketed_names : Str -> List(Str)
bracketed_names = |text|
	(Text.between(text, "[", "]") ?? "").split_on(",").map(Text.trim).keep_if(|name| name != "")
