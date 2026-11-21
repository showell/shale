## The checks of what a `??` falls back to:
##
## - `set_fallback`: `List.set(l, i, v) ?? l`, or `l.set(i, v) ?? l`, where
##   the fallback is the list being stored into, so every store copies.
## - `placeholder`: a fallback that is a string literal with nothing
##   interpolated (`?? "bogus"`, `?? ""`).
## - `number_fallback`: a fallback that is a number after a lookup by index
##   (`List.get(l, i) ?? 0`, `l.get(i) ?? 0`); with the types, after any
##   lookup that fails only with `[OutOfBounds]`.
##
## With the types, a finding names the fallback's type and the tags the
## lookup fails with. Each `??` is judged by the three in that order.
import pf.Shale
import Forest
import lib.Finding
import lib.RocTypes

Fallbacks := [].{

	## The types of the files reached by a root of the types: each file's real
	## path, and each expression's type by its file's real path, then its
	## region (RocTypes.span_of).
	Typing : { on : Bool, real : Dict(Str, Str), exprs : Dict(Str, Dict(Str, Str)) }

	## The findings, in the order of the `??`s.
	judge! : Forest, Fallbacks.Typing => List(Finding)
	judge! = |forest, typing| {
		var $findings = []
		$at : U64
		var $at = 0
		while $at < Forest.count(forest) {
			$at = $at + 1
			at = $at
			is_fallback = Forest.kind(forest, at) == "e-binop" and Forest.attribute(forest, at, "op") == "??"
			if is_fallback and Forest.kids(forest, at).len() == 2 {
				pair = {
					lookup: Forest.kid(forest, at, 1),
					fallback: Forest.kid(forest, at, 2),
					path: Forest.path(forest, at),
				}
				$findings = $findings.concat(judge_pair!(forest, typing, pair))
			}
		}
		$findings
	}
}

## A `??`'s two sides and its file.
Pair : { lookup : U64, fallback : U64, path : Str }

## What one `??` is: its set_fallback, its placeholder, its number_fallback.
judge_pair! : Forest, Fallbacks.Typing, Pair => List(Finding)
judge_pair! = |forest, typing, pair| {
	has_types = typing.on and typing.real.contains(pair.path)
	lookup_error = if has_types {
		lookup_error_of!(forest, typing, pair)
	} else {
		""
	}
	lookup_said = if lookup_error != "" {
		", for a lookup that fails with `${lookup_error}`"
	} else {
		""
	}
	line = Forest.line_number(forest, pair.fallback)
	at_fallback = |check, what| { path: pair.path, line, check, what }
	var $findings = []
	target = set_target(forest, pair.lookup)
	if target > 0 and is_same(forest, target, pair.fallback) {
		why = "the fallback names `l`, so every store copies; write `?? crash \"...\"` instead"
		cited = "(roc-apps CLAUDE.md, \"Six shapes copy\")"
		$findings = $findings.append(at_fallback("set_fallback", "`List.set(l, i, v) ?? l`: ${why} ${cited}"))
	}
	style = "(roc-apps docs/roc-style.md, \"Errors and failure\")"
	if is_literal_text(forest, pair.fallback) {
		text = Str.join_with(Forest.kids(forest, pair.fallback).map(|part| Forest.raw(forest, part)), "")
		typed = if has_types {
			", a Str${lookup_said}"
		} else {
			""
		}
		why = "if the lookup can't fail, `crash` with a reason, \"never a silent `?? default`\"; if it can, keep that case as a tag"
		$findings = $findings.append(at_fallback("placeholder", "falls back to the literal `\"${text}\"`${typed}: ${why} ${style}"))
	}
	is_judged_lookup = lookup_error == "[OutOfBounds]" or (!(has_types) and is_index_lookup(forest, pair.lookup))
	if is_number(forest, pair.fallback) and is_judged_lookup {
		number = Forest.raw(forest, pair.fallback)
		typed = if has_types {
			", a ${type_at(forest, typing, { path: pair.path, at: pair.fallback }) ?? "?"}"
		} else {
			""
		}
		why = "an index past the end is a bug, not a ${number}; `crash` with a reason, or keep the case as a tag"
		$findings = $findings.append(at_fallback("number_fallback", "falls back to `${number}`${typed}${lookup_said}: ${why} ${style}"))
	}
	$findings
}

## The tag union the lookup's `Try` fails with, such as `[OutOfBounds]`, or
## `""` when the checker gave it no such type.
lookup_error_of! : Forest, Fallbacks.Typing, Pair => Str
lookup_error_of! = |forest, typing, pair|
	match type_at(forest, typing, { path: pair.path, at: pair.lookup }) {
		Ok(lookup_type) =>
			match Shale.re_find!(lookup_type, "^Try\\((.*), (\\[[^\\[\\]]*\\])\\)$") {
				Found(matched) => matched.groups.get(1) ?? ""
				NoMatch => ""
			}
		Err(_) => ""
	}

## The checker's type of the expression at a place in a file it reached.
type_at : Forest, Fallbacks.Typing, { path : Str, at : U64 } -> Try(Str, [NoType])
type_at = |forest, typing, spot| {
	node = Forest.node(forest, spot.at)
	real = typing.real.get(spot.path) ?? ""
	span = RocTypes.span_of({ line: node.line, col: node.col, end_line: node.end_line, end_col: node.end_col })
	by_span = typing.exprs.get(real) ?? Dict.empty()
	match by_span.get(span) {
		Ok(written) => Ok(written)
		Err(_) => Err(NoType)
	}
}

## Whether the expressions at two places are the same value written twice
## (`board`, `r.b`): the same kinds, names and fields, all the way down.
is_same : Forest, U64, U64 -> Bool
is_same = |forest, first, second| {
	is_alike = |attribute| Forest.attribute(forest, first, attribute) == Forest.attribute(forest, second, attribute)
	first_kids = Forest.kids(forest, first)
	second_kids = Forest.kids(forest, second)
	if Forest.kind(forest, first) != Forest.kind(forest, second) or !(is_alike("raw")) or !(is_alike("field")) {
		Bool.False
	} else if first_kids.len() != second_kids.len() {
		Bool.False
	} else {
		List.map2(first_kids, second_kids, |a, b| (a, b)).all(|(a, b)| is_same(forest, a, b))
	}
}

## The list that the `List.set(l, i, v)` or `l.set(i, v)` call at a place
## stores into, 0 for any other expression.
set_target : Forest, U64 -> U64
set_target = |forest, at| {
	kind = Forest.kind(forest, at)
	kids = Forest.kids(forest, at)
	callee = Forest.kid(forest, at, 1)
	if kind == "e-apply" and kids.len() == 4 and Forest.kind_or_none(forest, callee) == "e-ident" and Forest.raw(forest, callee) == "List.set" {
		Forest.kid(forest, at, 2)
	} else if kind == "e-method-call" and Forest.attribute(forest, at, "method") == ".set" {
		is_receiver = |kid| Forest.kind(forest, kid) == "receiver" and Forest.kids(forest, kid).len() == 1
		match kids.find_first(is_receiver) {
			Ok(receiver) => Forest.kid(forest, receiver, 1)
			Err(_) => 0
		}
	} else {
		0
	}
}

## Whether the expression at a place is a string literal with nothing
## interpolated.
is_literal_text : Forest, U64 -> Bool
is_literal_text = |forest, at| {
	is_part = |kid| Forest.kind(forest, kid) == "e-string-part"
	Forest.kind(forest, at) == "e-string" and Forest.kids(forest, at).all(is_part)
}

## Whether the expression at a place is a lookup by index: `List.get(l, i)`
## or `l.get(i)`.
is_index_lookup : Forest, U64 -> Bool
is_index_lookup = |forest, at| {
	kind = Forest.kind(forest, at)
	callee = Forest.kid(forest, at, 1)
	if kind == "e-apply" and Forest.kind_or_none(forest, callee) == "e-ident" {
		Forest.raw(forest, callee) == "List.get"
	} else {
		kind == "e-method-call" and Forest.attribute(forest, at, "method") == ".get"
	}
}

is_number : Forest, U64 -> Bool
is_number = |forest, at| {
	kind = Forest.kind(forest, at)
	kind == "e-int" or kind == "e-frac"
}
