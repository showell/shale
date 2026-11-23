## The recipe's self-test, the Shale original's: each plant in
## cookbook/roc/plants/check_and_fix_group_stacking/ must give a finding of the kind its
## name starts with (`long`, `wide`, `unstack`, or else `stack`), a `long`
## plant no `wide string` and a `wide` plant no `long`, and a `clean*.roc`
## plant none. A plant line ending `# findings N` must give exactly N
## findings, and with `advice W` after it each of them must contain the word
## W. Then the plants are copied to `scratch:stack/` and rewritten there with
## the self-tests' Roc: the rewrite must end, a second rewrite must change
## nothing, every file must still parse, and a plant with a `.want` file must
## come out as it.
import pf.Shale
import Report
import Rewrite
import lib.Text

SelfTest := [].{

	## The plants, whose `*.roc` a self-test checks.
	plants : Str
	plants = "cookbook:roc/plants/check_and_fix_group_stacking"

	## The self-test of the plants, judged by `settings`.
	run! : List(Str), Report.Settings => {}
	run! = |paths, settings| {
		judged = Report.findings_in!(paths, settings)
		fail_if_refused!(judged.refused)
		marks_wrong = check_marks!(paths, judged)
		kinds_wrong = check_kinds!(paths, judged)
		wrong = marks_wrong + kinds_wrong
		if wrong > 0 {
			Shale.fail!("${wrong.to_str()} plants wrong")
		}
		if Shale.exists!(scratch) {
			Shale.remove_tree!(scratch)
		}
		Shale.mkdir!(scratch)
		copies = paths.map(copy_of)
		for path in paths {
			Shale.write!(copy_of(path), Report.read_text!(path))
		}
		roc = Rewrite.roc_bin!(Bool.True)
		given = { rule: settings.rule, roc }
		_first = Rewrite.trailing_commas!(copies, given)
		again = Rewrite.trailing_commas!(copies, given)
		if again > 0 {
			Shale.fail!("check_and_fix_group_stacking: a second rewrite changed ${again.to_str()} more groups")
		}
		formatted = Rewrite.format!(copies, roc)
		if formatted.code != 0 {
			Shale.print!(formatted.out)
			Shale.fail!("check_and_fix_group_stacking: a rewritten plant no longer parses")
		}
		$came_out_wrong : U64
		var $came_out_wrong = 0.U64
		for want in Shale.glob!("${plants}/*.want") {
			name = Text.basename(want).drop_suffix(".want")
			ok = Report.read_text!("${scratch}/${name}.roc") == Report.read_text!(want)
			Shale.print!("${verdict(ok)}${name}.roc: comes out as its .want")
			if !(ok) {
				$came_out_wrong = $came_out_wrong + 1
			}
		}
		if $came_out_wrong > 0 {
			Shale.fail!("${$came_out_wrong.to_str()} plants came out wrong")
		}
		Shale.print!("PASS: every plant gives its finding, the clean ones none; the rewrite ends, a second changes nothing, every plant parses and comes out as its .want")
	}

	## The files Roc's parser refused, named, then a stop
	## (cookbook/roc/parsed.shale's `fail_if_refused`); nothing when it
	## refused none.
	fail_if_refused! : List(Str) => {}
	fail_if_refused! = |refused| {
		if !(refused.is_empty()) {
			for refusal in refused {
				Shale.print!("refused: ${refusal}")
			}
			refusals = "roc-ast refused ${refused.len().to_str()} files (named above)"
			why = "which have no tree to judge; every other file was judged"
			Shale.fail!("check_and_fix_group_stacking: ${refusals}, ${why}")
		}
	}
}

## Where the self-test rewrites its copies of the plants.
scratch : Str
scratch = "scratch:stack"

## A plant's name, its path after the plants' folder.
name_of : Str -> Str
name_of = |path| path.drop_prefix("${SelfTest.plants}/")

## A plant's copy in `scratch`.
copy_of : Str -> Str
copy_of = |path| "${scratch}/${name_of(path)}"

## The start of a verdict's line.
verdict : Bool -> Str
verdict = |ok|
	if ok {
		"ok   "
	} else {
		"FAIL "
	}

## Each line marked `# findings N` (with `advice W`) held to the findings on
## it, a line printed for each: the answer is how many were wrong.
check_marks! : List(Str), Report.Judged => U64
check_marks! = |paths, judged| {
	$bad : U64
	var $bad = 0
	for path in paths {
		$number : U64
		var $number = 0
		for line in Text.lines(Report.read_text!(path)) {
			$number = $number + 1
			number = $number
			match Shale.re_find!(line, "# findings ([0-9]+)(?: advice ([a-z]+))?$") {
				Found(mark) => {
					(count_text, word) = match mark.groups {
						[count_given, word_given, ..] => (count_given, word_given)
						[count_given] => (count_given, "")
						[] => ("", "")
					}
					is_here = |finding| finding.path == path and finding.line == number
					here = judged.found.keep_if(is_here)
					got = here.len()
					var $ok = Ok(got.to_i64_wrap()) == I64.from_str(count_text)
					if word != "" {
						for finding in here {
							if !(finding.what.contains(word)) {
								$ok = Bool.False
							}
						}
					}
					place = "${name_of(path)}:${number.to_str()}"
					Shale.print!("${verdict($ok)}${place}: ${got.to_str()} findings, want ${count_text}")
					if !($ok) {
						$bad = $bad + 1
					}
				}
				NoMatch => {}
			}
		}
	}
	$bad
}

## Each plant held to the kinds of its findings by its name, a line printed
## for each: the answer is how many were wrong.
check_kinds! : List(Str), Report.Judged => U64
check_kinds! = |paths, judged| {
	$bad : U64
	var $bad = 0
	for path in paths {
		name = name_of(path)
		kinds = judged.found.keep_if(|finding| finding.path == path).map(|finding| finding.check)
		got = if kinds.is_empty() {
			""
		} else {
			" ${Str.join_with(kinds, " ")} "
		}
		is_long = name.starts_with("long")
		is_wide = name.starts_with("wide")
		want = if is_long {
			"long"
		} else if is_wide {
			"wide string"
		} else if name.starts_with("unstack") {
			"unstack"
		} else {
			"stack"
		}
		clean = name.starts_with("clean")
		var $ok = (clean and got == "") or (!(clean) and got.contains(" ${want} "))
		if is_long and got.contains("wide string") {
			$ok = Bool.False
		}
		if is_wide and got.contains("long") {
			$ok = Bool.False
		}
		shown = if got == "" {
			"no finding"
		} else {
			got
		}
		Shale.print!("${verdict($ok)}${name}: ${shown}")
		if !($ok) {
			$bad = $bad + 1
		}
	}
	$bad
}
