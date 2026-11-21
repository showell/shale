## The two checks, one for each place a retired word can be: `retired_name`,
## an identifier that holds one, read from Roc's own tree, and
## `retired_comment`, a comment that holds one, read from the file's lines.
import Finding
import Identifiers
import Text
import Tree
import Words

Retired := [].{

	## Each identifier in the tree that holds a retired word, a finding at
	## its node's line, once a line for each name and word.
	names : { path : Str, syntax_nodes : List(Tree.SyntaxNode) }, Words.Decided -> List(Finding)
	names = |file, decided| {
		var $found = []
		var $seen = Set.empty()
		var $place = 0
		for node in file.syntax_nodes {
			$place = $place + 1
			for name in Identifiers.of_node(file.syntax_nodes, node) {
				for word in Words.retired_in(name.to_utf8(), decided) {
					line = Tree.line_of(file.syntax_nodes, $place)
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
			match Text.comment_on_line(source_line) {
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
