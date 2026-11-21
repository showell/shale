## A finding of one of a recipe's checks: the file, the line, the check and
## what is wrong there, ordered as the Shale originals sorted their reports
## (cookbook/roc/gate_recipe.shale's `verdict_of`) and printed as their lines.
import Text

Finding := {
	path : Str,
	line : U64,
	check : Str,
	what : Str,
}.{

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

	## How many of the findings each check made, in the order of `checks`, as
	## a summary counts them: `family 2, twin_name 0`.
	counts_of : List(Finding), List(Str) -> Str
	counts_of = |findings, checks| {
		count_of = |check| findings.count_if(|finding| finding.check == check).to_str()
		Str.join_with(checks.map(|check| "${check} ${count_of(check)}"), ", ")
	}
}
