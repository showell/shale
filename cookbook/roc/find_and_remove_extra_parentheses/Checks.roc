## The four checks of find_and_remove_extra_parentheses, over Roc's tree of a file: the
## parentheses and braceless `if`s that a code emitter leaves (the launcher,
## cookbook/roc/find_and_remove_extra_parentheses.shale, says each rule). A finding names
## the node it stands on, so a rewrite (Edits) can take its brackets off.
import lib.Text
import lib.Tree

Checks := [].{

	## A finding: its check, its line, and the place of the node it stands
	## on, counted from 1 as the tree counts.
	Found : { check : Str, line : U64, at : U64 }

	## The checks, in the order the summary counts them and a rewrite makes
	## them.
	names : List(Str)
	names = ["paren_if", "wrapped_body", "else_block", "lambda_if"]

	## Each finding in a file, node by node in the tree's order; at one node,
	## `wrapped_body`, `paren_if`, `else_block`, then `lambda_if`.
	## `source_lines` is the file's text split at each newline.
	findings_in : List(Tree.Node), List(Str) -> List(Checks.Found)
	findings_in = |nodes, source_lines| {
		var $found = []
		$place : U64
		var $place = 1
		for node in nodes {
			at = $place
			if wraps_body(nodes, at) {
				$found = $found.append({ check: "wrapped_body", line: node.line, at })
			}
			if Checks.is_paren_if(nodes, at) {
				$found = $found.append({ check: "paren_if", line: node.line, at })
			}
			if is_else_block(nodes, at) {
				$found = $found.append({ check: "else_block", line: node.line, at })
			}
			if is_lambda_if(nodes, at, source_lines) {
				$found = $found.append({ check: "lambda_if", line: node.line, at })
			}
			$place = $place + 1
		}
		$found
	}

	## Whether a node is a pair of parentheses: a one-item tuple.
	is_parens : Tree.Node -> Bool
	is_parens = |node| node.kind == "e-tuple" and node.kids.len() == 1

	## Whether a branch is a block, in braces.
	has_braces : Tree.Node -> Bool
	has_braces = |node| node.kind == "e-block"

	## Whether an `else` branch needs no braces added: it has them, or it is
	## the next `if` of an `else if`.
	else_is_braced : Tree.Node -> Bool
	else_is_braced = |otherwise|
		Checks.has_braces(otherwise) or otherwise.kind == "e-if-then-else"

	## Whether the node at `at` is an `if` with its condition wholly in
	## parentheses, or a branch with no braces.
	is_paren_if : List(Tree.Node), U64 -> Bool
	is_paren_if = |nodes, at| {
		node = Tree.node_at(nodes, at)
		if node.kind != "e-if-then-else" {
			Bool.False
		} else {
			condition = Tree.node_at(nodes, Tree.kid_at(node, 1))
			then_branch = Tree.node_at(nodes, Tree.kid_at(node, 2))
			otherwise = Tree.node_at(nodes, Tree.kid_at(node, 3))
			Checks.is_parens(condition) or !(Checks.has_braces(then_branch)) or !(Checks.else_is_braced(otherwise))
		}
	}
}

## Whether the node at `at` is the last of its parent's kids.
is_last_kid : List(Tree.Node), U64 -> Bool
is_last_kid = |nodes, at| {
	node = Tree.node_at(nodes, at)
	Tree.last_kid(Tree.node_at(nodes, node.parent)) == at
}

## Whether parentheses at `at` wrap a whole body. The value of a declaration
## counts only over several lines; a statement counts wherever it stands, and
## `if`'s branches are paren_if's and the block's own.
wraps_body : List(Tree.Node), U64 -> Bool
wraps_body = |nodes, at| {
	node = Tree.node_at(nodes, at)
	if !(Checks.is_parens(node)) or node.parent == 0 {
		Bool.False
	} else {
		parent_kind = Tree.node_at(nodes, node.parent).kind
		if parent_kind == "statements" {
			Bool.True
		} else if parent_kind == "e-lambda" or parent_kind == "branch" {
			is_last_kid(nodes, at)
		} else if parent_kind == "s-decl" {
			is_last_kid(nodes, at) and node.end_line > node.line
		} else {
			Bool.False
		}
	}
}

## Whether the node at `at` is an `if` whose `else` is a block holding only
## another `if`: `else { if ... }`, which is `else if ...`.
is_else_block : List(Tree.Node), U64 -> Bool
is_else_block = |nodes, at| {
	node = Tree.node_at(nodes, at)
	if node.kind != "e-if-then-else" {
		Bool.False
	} else {
		otherwise = Tree.node_at(nodes, Tree.kid_at(node, 3))
		Checks.has_braces(otherwise) and holds_only_an_if(nodes, otherwise)
	}
}

## Whether a block's one statement is an `if`.
holds_only_an_if : List(Tree.Node), Tree.Node -> Bool
holds_only_an_if = |nodes, block| {
	statements = Tree.node_at(nodes, Tree.first_kid(block))
	match statements.kids {
		[only] => Tree.node_at(nodes, only).kind == "e-if-then-else"
		_ => Bool.False
	}
}

## Whether the node at `at` is an `if` that is a lambda's whole body and
## shares its line with what comes before it, which can only be the lambda's
## `|`.
is_lambda_if : List(Tree.Node), U64, List(Str) -> Bool
is_lambda_if = |nodes, at, source_lines| {
	node = Tree.node_at(nodes, at)
	if node.kind != "e-if-then-else" or node.parent == 0 {
		Bool.False
	} else if Tree.node_at(nodes, node.parent).kind != "e-lambda" or !(is_last_kid(nodes, at)) {
		Bool.False
	} else if Checks.is_paren_if(nodes, at) {
		Bool.False
	} else {
		line = Text.line_at(source_lines, node.line)
		before = Text.bytes_of(line, { start: 0, count: node.column - 1 })
		Text.trim(before) != ""
	}
}
