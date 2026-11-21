## The recipe's self-test, the Shale original's: each plant in
## cookbook/roc/plants/find_extra_parens/ held to its marks (a line ending
## `# paren_if`, `# wrapped_body`, `# else_block` or `# lambda_if` must give
## that finding, and no unmarked line may give one), then the plants copied
## to `scratch:parens/` and rewritten there: every file must still parse, no
## finding may be left, a second rewrite must change nothing, and a plant with
## a `.want` file must come out as it.
import pf.Shale
import Checks
import Report
import Rewriting
import lib.Finding
import lib.Text

SelfTest := [].{

	## The plants, whose `*.roc` a self-test checks.
	plants : Str
	plants = "cookbook:roc/plants/find_extra_parens"

	## The self-test of `paths`, the plants, given their report.
	run! : List(Str), Report.Report => {}
	run! = |paths, found| {
		marked = check_marks!(paths, found.findings)
		var $wrong = marked.wrong
		if marked.marks == 0 {
			$wrong = $wrong.append("no plant holds a mark")
		}
		if Shale.exists!(scratch) {
			Shale.remove_tree!(scratch)
		}
		Shale.mkdir!(scratch)
		copies = paths.map(copy_of)
		for path in paths {
			Shale.write!(copy_of(path), Report.read_text!(path))
		}
		roc = Rewriting.roc_bin!(Bool.True)
		rewritten = Rewriting.rewrite!(copies, roc)
		left = Report.make!(copies, rewritten.settled)
		for finding in left.findings {
			$wrong = $wrong.append("left after the rewrite: ${Finding.line_of(finding)}")
		}
		for refusal in left.refused {
			$wrong = $wrong.append("the rewrite broke a file: ${refusal}")
		}
		for path in paths {
			want = "${Text.bytes_of(path, { start: 0, count: path.count_utf8_bytes() - 4 })}.want"
			copy = copy_of(path)
			if Shale.exists!(want) and Report.read_text!(copy) != Report.read_text!(want) {
				$wrong = $wrong.append("${copy}: not as ${want}")
			}
		}
		if Rewriting.rewrite!(copies, roc).made != 0 {
			$wrong = $wrong.append("a second rewrite changed something")
		}
		Shale.remove_tree!(scratch)
		for wrong in $wrong {
			Shale.print!("FAIL ${wrong}")
		}
		if !($wrong.is_empty()) {
			Shale.fail!("find_extra_parens self_test: ${$wrong.len().to_str()} plants wrong")
		}
		proof = "each marked line gives its finding, no other line one; the rewrite leaves none, and a second changes nothing"
		Shale.print!("find_extra_parens (Roc) self_test: PASS (${proof})")
	}
}

## Where the self-test rewrites its copies of the plants.
scratch : Str
scratch = "scratch:parens"

## A plant's copy in `scratch`.
copy_of : Str -> Str
copy_of = |path| "${scratch}/${Text.basename(path)}"

## Each plant's lines held to the findings: what is wrong, and how many marks
## there were.
check_marks! : List(Str), List(Finding) => { wrong : List(Str), marks : U64 }
check_marks! = |paths, findings| {
	var $wrong = []
	$marks : U64
	var $marks = 0
	for path in paths {
		$number : U64
		var $number = 0
		for line in Text.lines(Report.read_text!(path)) {
			$number = $number + 1
			number = $number
			key = "${path}:${number.to_str()}"
			got = findings
				.keep_if(|finding| finding.path == path and finding.line == number)
				.map(|finding| finding.check)
			match Checks.names.find_first(|check| line.ends_with("# ${check}")) {
				Ok(mark) => {
					$marks = $marks + 1
					if !(got.contains(mark)) {
						$wrong = $wrong.append("${key}: no ${mark} finding")
					}
					if got.len() > 1 {
						$wrong = $wrong.append("${key}: more than the one finding marked")
					}
				}
				Err(_) =>
					if !(got.is_empty()) {
						$wrong = $wrong.append("${key}: a finding (${Str.join_with(got, ", ")}) on a line not marked for one")
					}
			}
		}
	}
	{ wrong: $wrong, marks: $marks }
}
