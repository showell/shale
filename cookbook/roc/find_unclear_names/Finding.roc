## A finding of one of the recipe's checks: the file, the line and what is
## unclear there, ordered as the Shale original sorted its report.
import Text

Finding := { path : Str, line : U64, check : Str, what : Str }.{

	## The checks, in the order the summary counts them.
	checks : List(Str)
	checks = ["pure_mutator", "abbreviation", "module_doc"]

	## By path, then line, then what is said, each path and saying bytewise.
	order : Finding, Finding -> [Before, Same, After]
	order = |a, b|
		match Text.byte_order(a.path, b.path) {
			Same =>
				if a.line < b.line {
					Before
				} else if a.line > b.line {
					After
				} else {
					Text.byte_order(a.what, b.what)
				}
			other => other
		}
}
