## The recipe's self-test: the plants in cookbook/roc/plants/find_deep_calls/,
## checked at `min=3` against the marks they carry. A line ending
## `# depth N` must give one finding of depth N, and one ending
## `# depth N, M` a finding for each, left to right; an added `from A to B`
## means the line's first finding must say `A to B`. No unmarked line may
## give a finding, no two findings may print the same line, and a
## `quiet_*.roc` plant may hold no mark.
import pf.Shale
import Finding

SelfTest := [].{

	## Where the plants are.
	plants : Str
	plants = "cookbook:roc/plants/find_deep_calls/"

	## Each plant's marks held to the findings the run printed: every fault
	## said, then a stop, or the PASS line.
	run! : { paths : List(Str), findings : List(Finding) } => {}
	run! = |{ paths, findings }| {
		var $faults = []
		var $mark_count = 0
		for path in paths {
			text = match Shale.read!(path) {
				Found(found) => found
				Missing => {
					Shale.fail!("find_deep_calls self_test: ${path} went missing")
					""
				}
			}
			var $number = 1
			for line in text.split_on("\n") {
				mark = read_mark!(line)
				if mark != Unmarked {
					$mark_count = $mark_count + 1
				}
				judged = {
					place: "${path}:${$number.to_str()}",
					path,
					mark,
					line_findings: findings_at_line(findings, { path, line: $number }),
				}
				$faults = $faults.concat(line_faults(judged))
				$number = $number + 1
			}
		}
		$faults = $faults.concat(duplicate_line_faults(findings))
		if $mark_count == 0 {
			$faults = $faults.append("no plant holds a `# depth` mark")
		}
		for fault in $faults {
			Shale.print!("FAIL ${fault}")
		}
		if !($faults.is_empty()) {
			Shale.fail!("find_deep_calls self_test: ${$faults.len().to_str()} plants wrong")
		}
		marks = "each marked line gives its depths left to right, the outermost only"
		others = "no other line a finding, and no two findings print alike"
		Shale.print!("find_deep_calls (Roc) self_test: PASS (${marks}, ${others})")
	}
}

## A line's mark: the depths it wants, as written (`3, 4`), and the
## outermost and deepest calls its first finding must name, if it says.
Mark : [Marked({ depths : Str, span : [Says(Str), Silent] }), Unmarked]

read_mark! : Str => Mark
read_mark! = |line| {
	depths = "([0-9]+(?:, [0-9]+)*)"
	span = "(?: from ([A-Za-z0-9_.]+) to ([A-Za-z0-9_.]+))?"
	match Shale.re_find!(line, "# depth ${depths}${span}$") {
		Found(found) =>
			match found.groups {
				[wanted, outermost, deepest] if outermost != "" =>
					Marked({ depths: wanted, span: Says("${outermost} to ${deepest}") })
				[wanted, ..] => Marked({ depths: wanted, span: Silent })
				[] => crash "find_deep_calls: a mark matched with no groups"
			}
		NoMatch => Unmarked
	}
}

## The findings on one line of a plant, left to right.
findings_at_line : List(Finding), { path : Str, line : U64 } -> List(Finding)
findings_at_line = |findings, { path, line }|
	findings
		.keep_if(|finding| finding.path == path and finding.nest.line == line)
		.sort_by(|finding| finding.nest.column)

## A plant's line to judge: where it is, its mark and its findings.
JudgedLine : {
	place : Str,
	path : Str,
	mark : Mark,
	line_findings : List(Finding),
}

## What is wrong with one line: its findings against its mark.
line_faults : JudgedLine -> List(Str)
line_faults = |{ place, path, mark, line_findings }| {
	got = Str.join_with(line_findings.map(|finding| finding.nest.depth.to_str()), ", ")
	match mark {
		Marked({ depths, span }) => {
			quiet_fault =
				if path.drop_prefix(SelfTest.plants).starts_with("quiet_") {
					["${place}: a quiet plant holds a `# depth` mark"]
				} else {
					[]
				}
			span_fault =
				match span {
					Says(says) =>
						match line_findings.first() {
							Ok(first) if Finding.head(first).contains(says) => []
							_ => ["${place}: the finding does not say `${says}`"]
						}
					Silent => []
				}
			depth_fault =
				if line_findings.is_empty() {
					["${place}: no finding, want depth ${depths}"]
				} else if got != depths {
					["${place}: depth ${got}, want ${depths}"]
				} else {
					[]
				}
			quiet_fault.concat(span_fault).concat(depth_fault)
		}
		Unmarked =>
			if line_findings.is_empty() {
				[]
			} else {
				["${place}: a finding (depth ${got}) on a line not marked for one"]
			}
	}
}

## A fault for each line that two findings print alike.
duplicate_line_faults : List(Finding) -> List(Str)
duplicate_line_faults = |findings| {
	finding_lines = findings.map(Finding.finding_line)
	is_printed_twice = |line| finding_lines.count_if(|other| other == line) > 1
	once_each = |seen, line| {
		if seen.contains(line) {
			seen
		} else {
			seen.append(line)
		}
	}
	finding_lines
		.keep_if(is_printed_twice)
		.fold([], once_each)
		.map(|line| "two findings print the same line, ${line}")
}
