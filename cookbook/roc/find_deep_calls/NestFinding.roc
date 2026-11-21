## A finding: one nest of calls in a file, and the source line its outermost
## call starts on, printed as
## `path:line:column: nested: depth N, <outermost> to <deepest>: <line>`.
import Nest
import lib.Text

NestFinding := {
	path : Str,
	nest : Nest,
	source_line : Str,
}.{

	## The finding up to its source line: where it is, how deep, and which
	## calls it runs between. The self-test reads this part.
	head : NestFinding -> Str
	head = |finding| {
		nest = finding.nest
		place = "${finding.path}:${nest.line.to_str()}:${nest.column.to_str()}"
		span = "${nest.outermost} to ${nest.deepest}"
		"${place}: nested: depth ${nest.depth.to_str()}, ${span}"
	}

	## The finding's whole line, its source line trimmed.
	finding_line : NestFinding -> Str
	finding_line = |finding|
		"${NestFinding.head(finding)}: ${Text.trim(finding.source_line)}"
}
