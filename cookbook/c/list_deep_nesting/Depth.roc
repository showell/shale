## How deeply a function's blocks nest, and the listing of the deep ones. A
## function's own body is depth 1, and each block a `{` opens inside it is
## one deeper. Each body line is read through the splitter's masker, so a
## brace in a string, a character literal or a comment is no brace, and a
## line's braces are counted net (`} else {` stays at its depth). A body
## that shares its head's line is depth 1.
import items.Item
import items.Mask
import items.Words

Depth := [].{

	## A function's body measured: where it starts and its name, its deepest
	## nesting, and the first line that reaches it.
	Measured : {
		path : Str,
		line : U64,
		name : Str,
		depth : U64,
		at : U64,
	}

	## The body of the item, a function or a test of the file at `path` whose
	## lines are `lines`, measured, or the stop's words when its braces close
	## past its body or don't come back to its own depth.
	of_body : Str, List(List(U8)), Item -> Try(Depth.Measured, Str)
	of_body = |path, lines, item| {
		start = { path, line: item.first, name: item.name, depth: 1, at: item.body_first }
		if item.body_first == item.sig_last {
			return Ok(start)
		}
		$depth : I64
		var $depth = 1
		$most : I64
		var $most = 1
		var $at = item.body_first
		var $number = item.body_first
		while $number <= item.body_last {
			code = Mask.masked(lines.get($number - 1) ?? [])
			opens = code.count_if(|byte| byte == '{').to_i64_wrap()
			closes = code.count_if(|byte| byte == '}').to_i64_wrap()
			$depth = $depth + opens - closes
			if $depth > $most {
				$most = $depth
				$at = $number
			}
			if $depth < 1 {
				return Err("${path}:${$number.to_str()}: `${item.name}`'s braces close past its body")
			}
			$number = $number + 1
		}
		if $depth != 1 {
			open = ($depth - 1).to_str()
			return Err("${path}:${item.first.to_str()}: `${item.name}`'s braces don't balance: ${open} left open at its end")
		}
		Ok({ ..start, depth: $most.to_u64_wrap(), at: $at })
	}

	## The listing of the measured functions at depth `min` or past, deepest
	## first, and the summary: the functions read, how many are at `min` or
	## past, and how many at each depth; or the stop's words when there is
	## no function.
	listing : List(Depth.Measured), I64 -> Try({ summary : Str, lines : List(Str) }, Str)
	listing = |functions, min| {
		if functions.is_empty() {
			return Err("list_deep_nesting: no function to measure")
		}
		depths = functions.map(|each| each.depth)
		counts = distinct(depths)
			.sort_with(up)
			.map(|depth| "${depth.to_str()}: ${depths.count_if(|each| each == depth).to_str()}")
		deep = functions
			.keep_if(|each| each.depth.to_i64_wrap() >= min)
			.sort_with(deepest_first)
		deep_lines = deep.map(
			|each| "${each.path}:${each.line.to_str()}: ${each.name}: depth ${each.depth.to_str()} (at line ${each.at.to_str()})",
		)
		read = "${functions.len().to_str()} functions read, ${deep.len().to_str()} at depth ${min.to_str()} or past"
		Ok({ summary: "${read}; by depth, ${Str.join_with(counts, ", ")}", lines: deep_lines })
	}
}

## Each depth once, in the order first seen.
distinct : List(U64) -> List(U64)
distinct = |depths|
	depths.fold(
		[],
		|seen, depth|
			if seen.contains(depth) {
				seen
			} else {
				seen.append(depth)
			},
	)

## Two depths, the shallower first.
up : U64, U64 -> [Before, Same, After]
up = |a, b|
	if a < b {
		Before
	} else if a > b {
		After
	} else {
		Same
	}

## Deepest first, then by path and line.
deepest_first : Depth.Measured, Depth.Measured -> [Before, Same, After]
deepest_first = |a, b|
	match up(b.depth, a.depth) {
		Same =>
			match Words.byte_order(a.path, b.path) {
				Same => up(a.line, b.line)
				order => order
			}
		order => order
	}
