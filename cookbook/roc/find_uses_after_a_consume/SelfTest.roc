## The recipe's self-test: the plants in
## cookbook/roc/plants/find_uses_after_a_consume/ (and its typed/ folder),
## each a copy shape from roc-apps' tools/copy_shapes/CopyShapes.roc. A line
## ending `# <check>` (or `# <check>, <check>`) must give exactly those
## findings, and no unmarked line may give one: so the member of each pair
## that writes in place stays clean, and the one that copies is found. A
## `quiet_*.roc` plant may hold no mark.
import pf.Shale
import lib.Finding
import lib.Text

SelfTest := [].{

	## Where the plants are.
	plants : Str
	plants = "cookbook:roc/plants/find_uses_after_a_consume/"

	## Each plant's marks held to the findings: every fault said, then a
	## stop, or the PASS line.
	run! : { paths : List(Str), findings : List(Finding) } => {}
	run! = |{ paths, findings }| {
		var $faults = []
		$marks : U64
		var $marks = 0
		for path in paths {
			text = match Shale.read!(path) {
				Found(found) => found
				Missing => {
					Shale.fail!("find_uses_after_a_consume self_test: ${path} went missing")
					""
				}
			}
			quiet = path.drop_prefix(SelfTest.plants).starts_with("quiet_")
			var $number = 1
			for line in text.split_on("\n") {
				wanted = mark_of!(line)
				got = findings
					.keep_if(|finding| finding.path == path and finding.line == $number)
					.map(|finding| finding.check)
					.sort_with(Text.byte_order)
				place = "${path}:${$number.to_str()}"
				if !(wanted.is_empty()) {
					$marks = $marks + 1
					if quiet {
						$faults = $faults.append("${place}: a quiet plant holds a mark")
					}
				}
				if got != wanted {
					$faults = $faults.append("${place}: want [${Str.join_with(wanted, ", ")}], got [${Str.join_with(got, ", ")}]")
				}
				$number = $number + 1
			}
		}
		if $marks == 0 {
			$faults = $faults.append("no plant holds a mark")
		}
		for fault in $faults {
			Shale.print!("FAIL ${fault}")
		}
		if !($faults.is_empty()) {
			Shale.fail!("find_uses_after_a_consume self_test: ${$faults.len().to_str()} lines wrong")
		}
		Shale.print!("find_uses_after_a_consume (Roc) self_test: PASS (${$marks.to_str()} marked lines each give their findings, no other line one)")
	}
}

## The checks a plant line's `# a, b` mark asks for, sorted; none for an
## unmarked line.
mark_of! : Str => List(Str)
mark_of! = |line|
	match Shale.re_find!(line, "# ((?:after_consume|whole_record|closure|held|restored_refcounted)(?:, [a-z_]+)*)$") {
		Found(found) =>
			(found.groups.first() ?? "")
				.split_on(", ")
				.sort_with(Text.byte_order)
		NoMatch => []
	}
