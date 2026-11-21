## What a Roc file's header says about who chooses its names, as
## cookbook/roc/headers.shale reads it: a name an app's platform asks for and
## a hook a platform hands its host were chosen somewhere else, so the
## recipes judge them differently from the names a program picks for itself.
import Bytes
import Text
import Tree

Headers := [].{

	## The first word of a file's code, its comment lines left out: `app`,
	## `package`, `platform`, `module`, `hosted`, or for a type module the
	## type's name; `""` when it has none (headers.shale's `header_word`).
	header_word : Str -> Str
	header_word = |text| {
		code = Text.lines(text).drop_if(|line| Text.trim(line).starts_with("#"))
		bytes = Str.join_with(code, "\n").to_utf8()
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

	## The value names a file's tree's header speaks for, in the tree's order
	## (headers.shale's `header_names`): `dictated`, what an app provides,
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
