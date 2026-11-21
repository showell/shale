## A finding of one of the recipe's checks, `family` or `twin_name`: the
## file, the line and what differs there, ordered as the Shale original
## sorted its report.
import Text

Finding := {
	path : Str,
	line : U64,
	check : Str,
	what : Str,
}.{

	## The checks, in the order the summary counts them.
	checks : List(Str)
	checks = ["family", "twin_name"]

	## By path, then line, then what is said, each path and saying bytewise.
	order : Finding, Finding -> [Before, Same, After]
	order = |first, second|
		match Text.byte_order(first.path, second.path) {
			Same =>
				if first.line < second.line {
					Before
				} else if first.line > second.line {
					After
				} else {
					Text.byte_order(first.what, second.what)
				}
			other => other
		}

	## The finding as its line: `path:line: check: what`.
	line_of : Finding -> Str
	line_of = |finding| {
		place = "${finding.path}:${finding.line.to_str()}"
		"${place}: ${finding.check}: ${finding.what}"
	}
}
