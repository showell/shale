## `write=1`: the files rewritten pass after pass until a pass changes
## nothing. A pass takes the trailing comma from each group that should
## unstack and gives one to each line's group that should stack, and `roc
## fmt` lays out the result: it stacks a group with a trailing comma and
## joins a group without one. The Roc is the tree's own
## (cookbook/roc/pinned.shale), run by its OS path.
import pf.Shale
import Report
import lib.Stacking
import lib.Text

Rewrite := [].{

	## Rewrites the files until a pass changes nothing, printing each pass,
	## and answers how many groups the first pass changed. Twelve passes
	## that still change something stop the run.
	trailing_commas! : List(Str), { rule : Stacking.Rule, roc : Str } => U64
	trailing_commas! = |paths, given| {
		$first_pass_count : U64
		var $first_pass_count = 0
		$pass : U64
		var $pass = 1
		$changed : U64
		var $changed = 1
		var $touched = []
		while $changed > 0 and $pass <= max_passes {
			$changed = 0
			$touched = []
			for path in paths {
				n = rewrite_file!(path, given.rule)
				if n > 0 {
					$touched = $touched.append(path)
					$changed = $changed + n
				}
			}
			Shale.print!("pass ${$pass.to_str()}: ${$changed.to_str()} groups rewritten, in ${$touched.len().to_str()} files")
			if $pass == 1 {
				$first_pass_count = $changed
			}
			if !($touched.is_empty()) {
				result = Rewrite.format!($touched, given.roc)
				if result.code != 0 {
					Shale.print!(result.out)
					Shale.fail!("roc fmt failed")
				}
			}
			$pass = $pass + 1
		}
		if $changed > 0 {
			Shale.fail!(
				"check_and_fix_group_stacking: still changing after ${max_passes.to_str()} passes: ${Str.join_with($touched, ", ")}",
			)
		}
		$first_pass_count
	}

	## `roc fmt` run on the files; its code is not 0 when a file does not
	## parse.
	format! : List(Str), Str => { code : I64, out : Str }
	format! = |paths, roc| {
		var $argv = [roc, "fmt"]
		for path in paths {
			$argv = $argv.append(Shale.real!(path))
		}
		Shale.run!({
			argv: $argv,
			keep: Tail(20),
			timeout_ms: 120000,
			want: "",
		})
	}

	## The OS path of the Roc to rewrite with: in a self-test the pin every
	## recipe's self-test runs (pinned.shale's `SELF_TEST_PIN`, read there),
	## else the one the `roc-version` where the run starts names.
	roc_bin! : Bool => Str
	roc_bin! = |self_test| {
		pin = if self_test {
			self_test_pin!({})
		} else {
			if !(Shale.exists!("roc-version")) {
				Shale.fail!("check_and_fix_group_stacking: no roc-version where the run starts: run it from the repository's root, whose roc-version pins its Roc")
			}
			Text.trim(Report.read_text!("roc-version"))
		}
		roc = "tools:roc-${pin}/roc"
		if !(Shale.exists!(roc)) {
			Shale.fail!("check_and_fix_group_stacking: no ${roc}: install Roc ${pin} into ~/tools/roc-${pin}/ (roc-apps's toolchain release carries it)")
		}
		real = Shale.real!(roc)
		said = Shale.run!({ argv: [real, "version"], keep: Whole(4000), timeout_ms: 30000, want: "" })
		if Text.trim(said.out) != "Roc compiler version ${pin}" {
			Shale.fail!("check_and_fix_group_stacking: ${roc} says ${Text.trim(said.out)}, not Roc compiler version ${pin}")
		}
		real
	}
}

## The most passes a rewrite makes.
max_passes : U64
max_passes = 12

## One pass over one file: the groups that should unstack lose their trailing
## commas, then each line's group that should stack gets one; the file is
## written when any changed, and the answer is how many did.
rewrite_file! : Str, Stacking.Rule => U64
rewrite_file! = |path, rule| {
	source_lines = Report.read_text!(path).split_on("\n")
	unstacked = match Stacking.groups_to_unstack(source_lines, rule) {
		Ok(found) => found
		Err(NoComma(line)) => {
			Shale.fail!("groups: line ${line.to_str()} does not end in its group's trailing comma")
			[]
		}
	}
	unstacked_lines = Stacking.without_trailing_commas(source_lines, unstacked)
	reachable = Stacking.without_targets_section(unstacked_lines)
	var $lines = unstacked_lines
	$n : U64
	var $n = unstacked.len()
	$index : U64
	var $index = 0
	for text in unstacked_lines {
		if !(Text.trim(text).starts_with("#")) {
			match Stacking.group_to_stack(reachable.get($index) ?? "", rule) {
				Ok(group) => {
					$lines = $lines.set(
						$index,
						Stacking.with_trailing_comma({ line: text, group }),
					) ?? crash "check_and_fix_group_stacking: a line past the file's end"
					$n = $n + 1
				}
				Err(_) => {}
			}
		}
		$index = $index + 1
	}
	if $n > 0 {
		Shale.write!(path, Str.join_with($lines, "\n"))
	}
	$n
}

## pinned.shale's `SELF_TEST_PIN`, the one home of the self-tests' Roc.
self_test_pin! : {} => Str
self_test_pin! = |{}| {
	pinned = "cookbook:roc/pinned.shale"
	groups = match Shale.re_find!(
		Report.read_text!(pinned),
		"\nconst SELF_TEST_PIN = \"([^\"]+)\"",
	) {
		Found(found) => found.groups
		NoMatch => []
	}
	match groups {
		[pin, ..] => pin
		[] => {
			Shale.fail!("check_and_fix_group_stacking: no SELF_TEST_PIN in ${pinned}")
			""
		}
	}
}
