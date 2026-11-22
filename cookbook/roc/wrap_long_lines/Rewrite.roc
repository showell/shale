## The rewrite of a folder: its .roc files copied to a work copy, then pass
## after pass, each a trailing comma given to each line's group that should
## stack and taken from each stacked group that should sit on one line, with
## `roc fmt` run on the work copy after each, until a pass changes nothing.
## The changed files are written back only once the rewrite has finished, so
## a rewrite that never settles leaves the folder as it was. The rule and its
## pieces are the cookbook's Roc library's Stacking; the Roc is the tree's
## own (cookbook/roc/pinned.shale), run by its OS path.
import pf.Shale
import lib.Bytes
import lib.Stacking
import lib.Text

Rewrite := [].{

	## What a rewrite runs by: the stacking rule (`tokens` 0 wraps wide lines
	## alone), the Roc's OS path, and the folder the work copies go in.
	Settings : { rule : Stacking.Rule, roc : Str, work_base : Str }

	## A finished rewrite: the work copy that holds it, and the folder's .roc
	## files, each as its path inside the folder.
	Done : { work : Str, paths : List(Str) }

	## The most passes a rewrite makes.
	max_passes : U64
	max_passes = 8

	## The work copy of folder `dir`: the self-test's is a name of its own,
	## any other's is named for the folder's OS path.
	work_of! : Str, { self_test : Bool, work_base : Str } => Str
	work_of! = |dir, given|
		if given.self_test {
			Text.path_under(given.work_base, "roc_commas_work/self_test")
		} else {
			named = Shale.re_replace!(Shale.real!(dir), "[^A-Za-z0-9]", "_")
			Text.path_under(given.work_base, "roc_commas_work/${named}")
		}

	## The .roc files of folder `dir`, each as its path inside it.
	roc_paths_in_dir! : Str => List(Str)
	roc_paths_in_dir! = |dir| {
		cut = dir.count_utf8_bytes() + 1
		Shale.glob!("${dir}/**/*.roc").map(|path| Text.bytes_of(path, { start: cut, count: path.count_utf8_bytes() }))
	}

	## Rewrites the work copy `work` of folder `dir` until a pass changes
	## nothing, printing each pass. A rewrite still changing after
	## `max_passes` passes is `StillChanging`, with the files the last pass
	## changed, named as the folder's.
	rewrite_in_work! : Str, { work : Str, settings : Rewrite.Settings } => Try(Rewrite.Done, [StillChanging(List(Str))])
	rewrite_in_work! = |dir, at| {
		work = at.work
		rule = at.settings.rule
		paths = Rewrite.roc_paths_in_dir!(dir)
		fresh_work_copy!(dir, { work, paths })
		run_roc_fmt!(work, at.settings.roc)
		$pass : U64
		var $pass = 1
		$changed : U64
		var $changed = 1
		var $touched = []
		# A file's text when a pass last left it alone. A later pass would
		# leave the same text alone again, so it is not read for groups twice.
		var $settled = Dict.empty()
		while $changed > 0 and $pass <= Rewrite.max_passes {
			$changed = 0
			$touched = []
			for path in paths {
				text = Rewrite.read_text!("${work}/${path}")
				if $settled.get(path) != Ok(text) {
					n = rewrite_file!("${work}/${path}", { text, rule })
					if n > 0 {
						$changed = $changed + n
						$touched = $touched.append("${dir}/${path}")
					} else {
						$settled = $settled.insert(path, text)
					}
				}
			}
			Shale.print!("${dir}: pass ${$pass.to_str()}: ${$changed.to_str()} trailing commas added or taken out")
			run_roc_fmt!(work, at.settings.roc)
			$pass = $pass + 1
		}
		if $changed > 0 {
			Err(StillChanging($touched))
		} else {
			Ok({ work, paths })
		}
	}

	## The stop a rewrite still changing makes, naming the files it changed.
	still_changing : List(Str) -> Str
	still_changing = |touched|
		"wrap_long_lines: still changing after ${Rewrite.max_passes.to_str()} passes, so no file was written: ${Str.join_with(touched, ", ")}"

	## Writes each file the rewrite changed back to folder `dir`, and says
	## how many.
	write_back! : Str, Rewrite.Done => {}
	write_back! = |dir, done| {
		var $changed = []
		for path in done.paths {
			text = Rewrite.read_text!("${done.work}/${path}")
			if text != Rewrite.read_text!("${dir}/${path}") {
				$changed = $changed.append({ path: "${dir}/${path}", text })
			}
		}
		for change in $changed {
			Shale.write!(change.path, change.text)
		}
		Shale.print!("${dir}: ${$changed.len().to_str()} files rewritten")
	}

	## A file's text; a file gone since the glob stops the run.
	read_text! : Str => Str
	read_text! = |path|
		match Shale.read!(path) {
			Found(text) => text
			Missing => {
				Shale.fail!("wrap_long_lines: no file ${path}")
				""
			}
		}

	## Whether a line is a comment's, which no trailing comma shortens.
	is_comment : Str -> Bool
	is_comment = |text| Text.trim(text).starts_with("#")

	## The OS path of the Roc to rewrite with: in a self-test the pin every
	## recipe's self-test runs (pinned.shale's `SELF_TEST_PIN`, read there),
	## else the one the `roc-version` where the run starts names.
	roc_bin! : Bool => Str
	roc_bin! = |self_test| {
		pin = if self_test {
			self_test_pin!({})
		} else {
			if !(Shale.exists!("roc-version")) {
				Shale.fail!("wrap_long_lines: no roc-version where the run starts: run it from the repository's root, whose roc-version pins its Roc")
			}
			Text.trim(Rewrite.read_text!("roc-version"))
		}
		roc = "tools:roc-${pin}/roc"
		if !(Shale.exists!(roc)) {
			Shale.fail!("wrap_long_lines: no ${roc}: install Roc ${pin} into ~/tools/roc-${pin}/ (roc-apps's toolchain release carries it)")
		}
		real = Shale.real!(roc)
		said = Shale.run!({ argv: [real, "version"], keep: Whole(4000), timeout_ms: 30000, want: "" })
		if Text.trim(said.out) != "Roc compiler version ${pin}" {
			Shale.fail!("wrap_long_lines: ${roc} says ${Text.trim(said.out)}, not Roc compiler version ${pin}")
		}
		real
	}
}

## The work copy made afresh: the folder's .roc files copied into it.
fresh_work_copy! : Str, { work : Str, paths : List(Str) } => {}
fresh_work_copy! = |dir, copy| {
	if Shale.exists!(copy.work) {
		Shale.remove_tree!(copy.work)
	}
	Shale.mkdir!(copy.work)
	for path in copy.paths {
		Shale.write!("${copy.work}/${path}", Rewrite.read_text!("${dir}/${path}"))
	}
}

## `roc fmt` run on the work copy; a file that does not parse stops the run,
## after its output.
run_roc_fmt! : Str, Str => {}
run_roc_fmt! = |work, roc| {
	result = Shale.run!({
		argv: [roc, "fmt", Shale.real!(work)],
		keep: Tail(20),
		timeout_ms: 60000,
		want: "",
	})
	if result.code != 0 {
		Shale.print!(result.out)
		Shale.fail!("roc fmt failed")
	}
}

## One pass over one file, whose text is `text`: the groups that should
## unstack lose their trailing commas, then each code line gets one for its
## group that should stack, if any; the file is written when any changed,
## and the answer is how many did.
rewrite_file! : Str, { text : Str, rule : Stacking.Rule } => U64
rewrite_file! = |path, given| {
	rule = given.rule
	source_lines = given.text.split_on("\n")
	unstacked = if rule.tokens > 0 {
		match Stacking.groups_to_unstack(source_lines, rule) {
			Ok(found) => found
			Err(NoComma(line)) => {
				Shale.fail!("groups: line ${line.to_str()} does not end in its group's trailing comma")
				[]
			}
		}
	} else {
		[]
	}
	unstacked_lines = Stacking.without_trailing_commas(source_lines, unstacked)
	reachable = Stacking.without_targets_section(unstacked_lines)
	var $lines = unstacked_lines
	$changed : U64
	var $changed = unstacked.len()
	$index : U64
	var $index = 0
	for text in unstacked_lines {
		if !(Rewrite.is_comment(text)) {
			match line_with_comma(reachable.get($index) ?? "", rule) {
				Ok(with_comma) => {
					$lines = $lines.set($index, with_comma) ?? crash "wrap_long_lines: a line past the file's end"
					$changed = $changed + 1
				}
				Err(_) => {}
			}
		}
		$index = $index + 1
	}
	if $changed > 0 {
		Shale.write!(path, Str.join_with($lines, "\n"))
	}
	$changed
}

## The line with a trailing comma for its outermost group that should
## stack, or else, when it is wider than the width, for the group that
## stacking brings within it. A line of data gets none.
line_with_comma : Str, Stacking.Rule -> Try(Str, [NoComma])
line_with_comma = |text, rule| {
	few_commas = rule.tokens == 0 or commas_before_the_last(text) < Stacking.stack_items - 1
	if Bytes.line_width(text) <= rule.width and few_commas {
		Err(NoComma)
	} else {
		match Stacking.group_to_stack(text, rule) {
			Ok(group) => Ok(Stacking.with_trailing_comma({ line: text, group }))
			Err(_) => Err(NoComma)
		}
	}
}

## How many commas a line holds, not counting one that ends it. A group of
## `stack_items` items that opens and closes on the line holds one fewer at
## least, so a line that fits with fewer has no group to stack. Counting is
## far cheaper than reading the line's groups, and most of a data file's
## lines are like `{ x: 0.5, y: 0.25 },`.
commas_before_the_last : Str -> U64
commas_before_the_last = |text| {
	bytes = text.to_utf8()
	commas = bytes.count_if(|byte| byte == ',')
	if Bytes.trimmed_end(bytes).last() == Ok(',') {
		commas - 1
	} else {
		commas
	}
}

## pinned.shale's `SELF_TEST_PIN`, the one home of the self-tests' Roc.
self_test_pin! : {} => Str
self_test_pin! = |{}| {
	pinned = "cookbook:roc/pinned.shale"
	groups = match Shale.re_find!(
		Rewrite.read_text!(pinned),
		"\nconst SELF_TEST_PIN = \"([^\"]+)\"",
	) {
		Found(found) => found.groups
		NoMatch => []
	}
	match groups {
		[pin, ..] => pin
		[] => {
			Shale.fail!("wrap_long_lines: no SELF_TEST_PIN in ${pinned}")
			""
		}
	}
}
