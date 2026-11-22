## The recipe's self-test, the Shale original's: the plants in
## cookbook/roc/plants/wrap_long_lines/ are copied into the work folder and
## rewritten there with the rule's six tokens and the self-tests' Roc. The
## rewrite must end, each plant must come out as its `.want` file, no group
## may be left to stack or to unstack, and the lines left wide must be
## exactly `long_plants`' listed, with `wide_string_plants` wide strings and
## `data_plants` lines of data counted. Then `too_deep/too_deep.roc`, one
## group deeper than the passes reach, must stop the rewrite, naming its
## file, and be left as it was.
import pf.Shale
import lib.Stacking
import lib.Text
import Rewrite
import WideLines

SelfTest := [].{

	## The self-test, its copies and work copies under `settings.work_base`.
	run! : Rewrite.Settings => {}
	run! = |settings| {
		dir = Text.path_under(settings.work_base, "wrap_long_lines")
		work = Rewrite.work_of!(dir, { self_test: Bool.True, work_base: settings.work_base })
		fresh_copies!(dir, plants)
		match Rewrite.rewrite_in_work!(dir, { work, settings }) {
			Ok(done) => {
				Rewrite.write_back!(dir, done)
				Shale.remove_tree!(done.work)
			}
			Err(StillChanging(touched)) => Shale.fail!(Rewrite.still_changing(touched))
		}
		$bad : U64
		var $bad = 0
		for plant in Shale.glob!("${plants}/*.roc") {
			$bad = $bad + plant_wrong!(plant, { dir, rule: settings.rule })
		}
		$bad = $bad + report_wrong!(dir, settings.rule.width)
		if $bad > 0 {
			Shale.fail!("${$bad.to_str()} checks failed")
		}
		if Shale.exists!(work) {
			Shale.fail!("wrap_long_lines: a finished rewrite should remove its work copy, ${work}")
		}
		fresh_copies!(dir, "${plants}/too_deep")
		named = Rewrite.still_changing(["${dir}/too_deep.roc"])
		stopped = match Rewrite.rewrite_in_work!(dir, { work, settings }) {
			Ok(_) => Bool.False
			Err(StillChanging(touched)) => Rewrite.still_changing(touched) == named
		}
		if !(stopped) {
			Shale.fail!("wrap_long_lines: too_deep.roc should stop with `${named.drop_prefix("wrap_long_lines: ")}`")
		}
		copy = Rewrite.read_text!("${dir}/too_deep.roc")
		if copy != Rewrite.read_text!("${plants}/too_deep/too_deep.roc") {
			Shale.fail!("wrap_long_lines: a rewrite that stops should leave too_deep.roc as it was")
		}
		Shale.print!("ok   too_deep.roc: stops naming its file, and is left as it was")
		Shale.remove_tree!(work)
		Shale.remove_tree!(dir)
		Shale.print!("PASS: the rewrite ends, every plant comes out as its .want, no group is left to stack or to unstack, the lines left wide are listed or counted, and a rewrite past its passes stops naming its file and changes none")
	}
}

## The plants, whose `*.roc` the self-test rewrites.
plants : Str
plants = "cookbook:roc/plants/wrap_long_lines"

## The plant lines left wide with nothing to stack.
long_plants : List(Str)
long_plants = ["function_type.roc:4"]

## How many plant lines are wide only for their strings.
wide_string_plants : U64
wide_string_plants = 1

## How many plant lines are numeric data.
data_plants : U64
data_plants = 2

## The start of a verdict's line.
verdict : Bool -> Str
verdict = |ok|
	if ok {
		"ok   "
	} else {
		"FAIL "
	}

## Folder `dir` made afresh with a copy of each `*.roc` in folder `from`.
fresh_copies! : Str, Str => {}
fresh_copies! = |dir, from| {
	if Shale.exists!(dir) {
		Shale.remove_tree!(dir)
	}
	Shale.mkdir!(dir)
	for plant in Shale.glob!("${from}/*.roc") {
		Shale.write!("${dir}/${Text.basename(plant)}", Rewrite.read_text!(plant))
	}
}

## One plant's rewrite held to its `.want`, and to no group left to stack or
## to unstack, a line printed for each wrong: the answer is how many were.
plant_wrong! : Str, { dir : Str, rule : Stacking.Rule } => U64
plant_wrong! = |plant, given| {
	name = Text.basename(plant)
	want = "${plants}/${name.drop_suffix(".roc")}.want"
	got = Rewrite.read_text!("${given.dir}/${name}")
	ok = got == Rewrite.read_text!(want)
	Shale.print!("${verdict(ok)}${name}: comes out as its .want")
	$bad : U64
	var $bad = if ok {
		0
	} else {
		1
	}
	got_lines = got.split_on("\n")
	$number : U64
	var $number = 0
	for text in Stacking.without_targets_section(got_lines) {
		$number = $number + 1
		left = !(Rewrite.is_comment(text)) and Stacking.group_to_stack(text, given.rule) != Err(NoGroup)
		if left {
			Shale.print!("FAIL ${name}:${$number.to_str()}: a group left to stack: ${Text.trim(text)}")
			$bad = $bad + 1
		}
	}
	unstacked = match Stacking.groups_to_unstack(got_lines, given.rule) {
		Ok(found) => found
		Err(NoComma(line)) => {
			Shale.fail!("groups: line ${line.to_str()} does not end in its group's trailing comma")
			[]
		}
	}
	for found in unstacked {
		Shale.print!("FAIL ${name}:${found.group.line.to_str()}: a group left to unstack")
		$bad = $bad + 1
	}
	$bad
}

## The lines the rewrite left wide held to `long_plants`, and the wide
## strings and data counted, a line printed for each: the answer is how many
## were wrong.
report_wrong! : Str, U64 => U64
report_wrong! = |dir, width| {
	report = WideLines.report_wide_lines!(
		WideLines.fresh,
		{ dir, at: dir, paths: Rewrite.roc_paths_in_dir!(dir), width },
	)
	cut = dir.count_utf8_bytes() + 1
	listed = report.long.map(
		|line| {
			place = Text.before_first(line, " ")
			Text.bytes_of(place, { start: cut, count: place.count_utf8_bytes() })
		},
	)
	list_ok = Text.sort_bytewise(listed) == Text.sort_bytewise(long_plants)
	Shale.print!("${verdict(list_ok)}the list of lines left wide is ${Str.join_with(long_plants, ", ")} (${Str.join_with(report.long, "; ")})")
	counts = "${report.wide_strings.to_str()} lines wide only for their strings and ${report.data.to_str()} of numeric data"
	wanted = "want ${wide_string_plants.to_str()} and ${data_plants.to_str()} (${report.refused.len().to_str()} refused)"
	counts_ok = report.wide_strings == wide_string_plants and report.data == data_plants and report.refused.is_empty()
	Shale.print!("${verdict(counts_ok)}${counts}, counted and not listed, ${wanted}")
	$bad : U64
	var $bad = 0
	if !(list_ok) {
		$bad = $bad + 1
	}
	if !(counts_ok) {
		$bad = $bad + 1
	}
	$bad
}
