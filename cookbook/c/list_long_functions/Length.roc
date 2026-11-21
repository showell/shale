## A function's length and the listing of the long ones. A function's
## length is its body's lines of code: a blank line and a `//` line are
## counted beside it, and a body that shares its head's line
## (`{ return x; }`) is one line of code. A `#` line in a body is code.
import items.Item
import items.Mask
import items.Words

Length := [].{

	## A function's body measured: where it starts and its name, then its
	## lines of code, of comment and blank.
	Measured : {
		path : Str,
		line : U64,
		name : Str,
		code : U64,
		comment : U64,
		blank : U64,
	}

	## The body of the item, a function or a test of the file at `path`
	## whose lines are `lines`, measured.
	of_body : Str, List(List(U8)), Item -> Length.Measured
	of_body = |path, lines, item| {
		start = { path, line: item.first, name: item.name, code: 0, comment: 0, blank: 0 }
		if item.body_first == item.sig_last {
			{ ..start, code: 1 }
		} else {
			body = lines.sublist({ start: item.body_first - 1, len: item.body_last + 1 - item.body_first })
			body.fold(start, with_line)
		}
	}

	## The listing of the measured functions past the limit (`Percentile`:
	## the 95th percentile of their lengths): the distribution, the
	## threshold and a line for each function past it, longest first, or the
	## stop's words when there is no function.
	listing : List(Length.Measured), [Given(U64), Percentile] -> Try({ summary : Str, threshold : U64, lines : List(Str) }, Str)
	listing = |functions, limit| {
		lengths = functions.map(|each| each.code).sort_with(up)
		if lengths.is_empty() {
			return Err("list_long_functions: no function to measure")
		}
		at = |pct| percentile(lengths, pct).to_str()
		longest = (lengths.last() ?? 0).to_str()
		shares = "median ${at(50)}, p75 ${at(75)}, p90 ${at(90)}, p95 ${at(95)}, p99 ${at(99)}"
		summary = "${lengths.len().to_str()} functions: ${shares}, max ${longest} code lines"
		threshold = match limit {
			Given(over) => over
			Percentile => percentile(lengths, 95)
		}
		long_lines = functions
			.keep_if(|each| each.code > threshold)
			.sort_with(longest_first)
			.map(long_line)
		Ok({ summary, threshold, lines: long_lines })
	}
}

## Two lengths, the shorter first.
up : U64, U64 -> [Before, Same, After]
up = |a, b|
	if a < b {
		Before
	} else if a > b {
		After
	} else {
		Same
	}

## The measure with one more of the body's lines counted.
with_line : Length.Measured, List(U8) -> Length.Measured
with_line = |so_far, raw| {
	trimmed = Mask.trimmed(raw)
	if trimmed.is_empty() {
		{ ..so_far, blank: so_far.blank + 1 }
	} else if trimmed.starts_with(['/', '/']) {
		{ ..so_far, comment: so_far.comment + 1 }
	} else {
		{ ..so_far, code: so_far.code + 1 }
	}
}

## The value at a percentile of the lengths, sorted up: the one at rank
## ceil(pct n / 100), counted from 1.
percentile : List(U64), U64 -> U64
percentile = |sorted, pct| {
	rank = U64.max(1, (pct * sorted.len() + 99) // 100)
	sorted.get(rank - 1) ?? 0
}

## Longest first, then by path and line.
longest_first : Length.Measured, Length.Measured -> [Before, Same, After]
longest_first = |a, b|
	if a.code > b.code {
		Before
	} else if a.code < b.code {
		After
	} else {
		match Words.byte_order(a.path, b.path) {
			Same =>
				if a.line < b.line {
					Before
				} else if a.line > b.line {
					After
				} else {
					Same
				}
			order => order
		}
	}

## A long function's line: `path:line: long: name: N code lines (C
## comment, B blank)`.
long_line : Length.Measured -> Str
long_line = |each| {
	counts = "${each.code.to_str()} code lines (${each.comment.to_str()} comment, ${each.blank.to_str()} blank)"
	"${each.path}:${each.line.to_str()}: long: ${each.name}: ${counts}"
}
