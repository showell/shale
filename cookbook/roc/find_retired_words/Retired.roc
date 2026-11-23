## The two checks, one for each place a retired word can be: `retired_name`,
## an identifier that holds one, read from Roc's own tree, and
## `retired_comment`, a comment that holds one, read from the file's lines.
import lib.Finding
import Identifiers
import lib.Text
import lib.Tree
import Words

Retired := [].{

	## Each identifier in the tree that holds a retired word, a finding at
	## its node's line, once a line for each name and word.
	names : { path : Str, syntax_nodes : List(Tree.Node) }, Words.Decided -> List(Finding)
	names = |file, decided| {
		var $found = []
		var $seen = Set.empty()
		var $place = 0
		for node in file.syntax_nodes {
			$place = $place + 1
			for name in Identifiers.of_node(file.syntax_nodes, node) {
				for word in Words.retired_in(name.to_utf8(), decided) {
					line = line_of(file.syntax_nodes, $place)
					key = "${line.to_str()}|${name}|${word}"
					if !($seen.contains(key)) {
						$seen = $seen.insert(key)
						what = "${name}: ${word} is ${Words.kept_for(decided, word)} here"
						$found = $found.append({ path: file.path, line, check: "retired_name", what })
					}
				}
			}
		}
		$found
	}

	## Each retired word in a comment of the file's text, a finding at its
	## line, once a line.
	comments : { path : Str, text : Str }, Words.Decided -> List(Finding)
	comments = |file, decided| {
		var $found = []
		var $number = 0
		for source_line in Text.lines(file.text) {
			$number = $number + 1
			match comment_on_line(source_line) {
				Comment(comment) =>
					for word in Words.retired_in(comment, decided) {
						what = "${word} is ${Words.kept_for(decided, word)} here"
						$found = $found.append({ path: file.path, line: $number, check: "retired_comment", what })
					}
				NoComment => {}
			}
		}
		$found
	}
}

## The line of the node at `place`, or of the nearest node around it that
## has one; 0 when none does (the Shale original's `node_line`).
line_of : List(Tree.Node), U64 -> U64
line_of = |syntax_nodes, place| {
	var $place = place
	var $line = 0
	while $place != 0 and $line == 0 {
		node = Tree.node_at(syntax_nodes, $place)
		if node.line == 0 {
			$place = node.parent
		} else {
			$line = node.line
		}
	}
	$line
}

## The bytes after the `#` that starts the line's comment, or `NoComment`.
## A `#` inside a string or a character starts none, and a line of a `\\`
## multiline string has no comment at all.
comment_on_line : Str -> [Comment(List(U8)), NoComment]
comment_on_line = |line| {
	bytes = line.to_utf8()
	if !(bytes.contains('#')) {
		return NoComment
	}
	count = bytes.len()
	start = bytes.find_first_index(|byte| !(is_space(byte))) ?? count
	if (bytes.get(start) ?? 0) == '\\' and (bytes.get(start + 1) ?? 0) == '\\' {
		return NoComment
	}
	var $at = 0
	while $at < count {
		byte = bytes.get($at) ?? 0
		if byte == '"' or byte == '\'' {
			$at = $at + 1
			while $at < count and (bytes.get($at) ?? 0) != byte {
				if (bytes.get($at) ?? 0) == '\\' {
					$at = $at + 1
				}
				$at = $at + 1
			}
		} else if byte == '#' {
			return Comment(bytes.drop_first($at + 1))
		}
		$at = $at + 1
	}
	NoComment
}

## A byte Shale's `trim` takes off a text's ends: a space, a tab, a carriage
## return or a newline.
is_space : U8 -> Bool
is_space = |byte| byte == ' ' or byte == '\t' or byte == '\r' or byte == '\n'
