## The recipe's self-test: each plant in
## cookbook/roc/plants/rewrite_method_calls/ judged in a child run of its own,
## through the launcher (`plant=<Name>`), so each plant gets a run's
## programs of its own. The child copies the plants to a fresh folder under
## `scratch:`, runs there with `apply=1` and checks what came out against
## the plant's expectation (Plants).
import pf.Shale
import Compiler
import Plants
import Run
import Sweep
import Types
import lib.Text

SelfTest := [].{

	## Every plant in a child run of its own; a failed plant is named, and
	## the rest still run.
	run! : {} => {}
	run! = |{}| {
		if !(Shale.exists!(Types.default_checker)) {
			Shale.fail!("rewrite_method_calls: self-test: no ${Types.default_checker}, which the plants marked `types` are judged by: `shale install.shale` in a checkout of https://github.com/showell/roc-types installs it")
		}
		var $failed = []
		globbed = SelfTest.paths_of_globs!("${plants}/Proven.roc, ${plants}/NoC*.roc,${plants}/Proven.roc")
		held = globbed.len() == 2 and Text.basename(globbed.get(0) ?? "") == "Proven.roc" and Text.basename(globbed.get(1) ?? "") == "NoCalls.roc"
		Shale.print!("${verdict(held)}glob=: two globs and a repeat give Proven.roc, then NoCalls.roc, each once (${Str.join_with(globbed, ", ")})")
		if !(held) {
			$failed = $failed.append("glob=")
		}
		for name in listed!(plants) {
			plant = name.drop_suffix(".roc")
			judged = Plants.table.any(|want| want.name == plant) or Plants.root_plants.contains(plant)
			if name.ends_with(".roc") and !(judged) {
				Shale.print!("FAIL  ${name}: a plant the table does not judge")
				$failed = $failed.append(name)
			}
		}
		for want in Plants.table.sort_with(|first, second| Text.byte_order(first.name, second.name)) {
			ran = Shale.run!({
				argv: ["shale", "--root", ".", launcher, "self_test=1", "plant=${want.name}"],
				keep: Tail(200),
				timeout_ms: 3600000,
				want: "",
			})
			if want.stop != "" {
				stopped = ran.code > 0 and ran.out.contains(want.stop)
				shown = if stopped {
					""
				} else {
					": ${Text.trim(ran.out)}"
				}
				Shale.print!("${verdict(stopped)}${want.name}: stops, saying ${want.stop}${shown}")
				if !(stopped) {
					$failed = $failed.append(want.name)
				}
			} else {
				for said in Text.lines(ran.out) {
					if !(said.starts_with("── budgets")) {
						Shale.print!(said)
					}
				}
				if ran.code != 0 {
					$failed = $failed.append(want.name)
				}
			}
		}
		if !($failed.is_empty()) {
			Shale.fail!("rewrite_method_calls: self-test: ${$failed.len().to_str()} plant(s) failed: ${Str.join_with($failed, ", ")}")
		}
		Shale.print!("rewrite_method_calls: self-test: every plant as it says")
	}

	## The child run `plant=<name>`: the plants copied to a fresh scratch
	## folder, the one plant run there with `apply=1`, and the result checked
	## against its expectation.
	plant! : Str, Str => {}
	plant! = |name, self_test_roc| {
		want = match Plants.table.find_first(|plant_want| plant_want.name == name) {
			Ok(found) => found
			Err(_) => {
				Shale.fail!("rewrite_method_calls: self-test: no plant ${name} in the table")
				Plants.table.first() ?? crash "SelfTest: the table is empty"
			}
		}
		scratch = Sweep.fresh_dir!("methods")
		for listed_name in listed!(plants) {
			Shale.write!("${scratch}/${listed_name}", Compiler.read_text!("${plants}/${listed_name}"))
		}
		file = if want.file == "" {
			name
		} else {
			want.file
		}
		path = "${scratch}/${file}.roc"
		paths = if want.beside == "" {
			[path]
		} else {
			[path, "${scratch}/${want.beside}.roc"]
		}
		# A plant with `tree_pin` runs as though the tree pinned that Roc.
		types_roc = if want.tree_pin == "" {
			self_test_roc
		} else {
			Shale.real!("tools:roc-${want.tree_pin}/roc")
		}
		roots = want.root.split_on(",").keep_if(|root| root != "").map(|root| "${scratch}/${root}.roc")
		# A plant with `types_from` reads the types from a file, as a gate
		# keeps them: roc-types' output on the root, written here first.
		types_from = if want.types_from {
			written = "${scratch}/root.types"
			ran = Shale.run!({
				argv: [Shale.real!(Types.checker_for!(types_roc)), Shale.real!(roots.first() ?? "")],
				keep: Whole(64000000),
				timeout_ms: 300000,
				want: "",
			})
			if ran.code != 0 {
				Shale.print!(ran.out)
				Shale.fail!("rewrite_method_calls: self-test: roc-types exited ${ran.code.to_str()}")
			}
			Shale.write!(written, ran.out)
			written
		} else {
			""
		}
		got = Run.all!({
			paths,
			root: Str.join_with(roots, ","),
			from: 1,
			use_types: want.types,
			types_roc,
			judge: { roc: self_test_roc, apply: Bool.True },
			count_only: want.count,
			types_from,
			undo: want.undo,
		})
		if want.stop != "" {
			Shale.fail!("rewrite_method_calls: self-test: ${name} ran to its end, and was to stop: ${want.stop}")
		}
		checks = checks_of!({ want, got, path, plant_file: "${plants}/${file}.roc", roots, types_from, paths })
		failures = checks.count_if(|(passed, _)| !(passed))
		for (passed, what) in checks {
			Shale.print!("${verdict(passed)}${what}")
		}
		if failures > 0 {
			Shale.fail!("rewrite_method_calls: self-test: ${name}: ${failures.to_str()} failed")
		}
		Shale.remove_tree!(scratch)
	}

	## The files of `globs`, several separated by commas, in the order the
	## globs give them, each once.
	paths_of_globs! : Str => List(Str)
	paths_of_globs! = |globs| {
		var $paths = []
		var $seen = Set.empty()
		for pattern in globs.split_on(",") {
			for path in Shale.glob!(Text.trim(pattern)) {
				if !($seen.contains(path)) {
					$seen = $seen.insert(path)
					$paths = $paths.append(path)
				}
			}
		}
		$paths
	}
}

## The launcher each plant's child run goes through.
launcher : Str
launcher = "cookbook:roc/rewrite_method_calls.shale"

plants : Str
plants = "cookbook:roc/plants/rewrite_method_calls"

verdict : Bool -> Str
verdict = |passed|
	if passed {
		"ok    "
	} else {
		"FAIL  "
	}

## The names in a folder, as Shale's `ls` gives them.
listed! : Str => List(Str)
listed! = |folder|
	match Shale.list!(folder) {
		Found(names) => names
		Missing => {
			Shale.fail!("rewrite_method_calls: self-test: no ${folder}")
			[]
		}
	}

## Each check of a plant's child run, in the Shale original's order: whether
## it passed and what it says.
checks_of! : { want : Plants.Expect, got : Run.Total, path : Str, plant_file : Str, roots : List(Str), types_from : Str, paths : List(Str) } => List((Bool, Str))
checks_of! = |asked| {
	want = asked.want
	got = asked.got
	name = want.name
	var $checks = []
	if want.count {
		$checks = $checks.append((got.rewritten == want.proven, "${name}: ${got.rewritten.to_str()} proven, want ${want.proven.to_str()}"))
		$checks = $checks.append((got.unsettled == want.unsettled, "${name}: ${got.unsettled.to_str()} unsettled, want ${want.unsettled.to_str()}"))
		$checks = $checks.append((Compiler.read_text!(asked.path) == Compiler.read_text!(asked.plant_file), "${name}: counted, and left as it was"))
	}
	match want.peers {
		Peers(peers) => {
			$checks = $checks.append((got.peers == peers, "${name}: ${got.peers.to_str()} peer_receiver, want ${peers.to_str()}"))
		}
		AnyPeers => {}
	}
	made = Sweep.made!(got.tally)
	listed = if made.lines.is_empty() {
		""
	} else {
		" (${Str.join_with(made.lines, "; ")})"
	}
	made_held = made.unused == want.made_unused and made.long == want.made_long
	made_counts = "${made.unused.to_str()} made_unused_import and ${made.long.to_str()} made_long_line"
	$checks = $checks.append((made_held, "${name}: ${made_counts}, want ${want.made_unused.to_str()} and ${want.made_long.to_str()}${listed}"))
	if want.made_says != "" {
		$checks = $checks.append((Str.join_with(made.lines, "\n").contains(want.made_says), "${name}: reports ${want.made_says}"))
	}
	if asked.types_from != "" {
		used = got.tally.programs_used
		$checks = $checks.append((used == 1, "${name}: the types read from a file, so roc-ast the one program (${used.to_str()})"))
	}
	unreached = got.unreached
	$checks = $checks.append((unreached.len() == want.unreached, "${name}: ${unreached.len().to_str()} not reached by a root, want ${want.unreached.to_str()} (${Str.join_with(unreached, ", ")})"))
	left = got.lines
	left_listed = if left.is_empty() {
		""
	} else {
		" (${Str.join_with(left, "; ")})"
	}
	$checks = $checks.append((left.len() == want.left, "${name}: ${left.len().to_str()} left, want ${want.left.to_str()}${left_listed}"))
	if want.why != "" {
		$checks = $checks.append((!(left.is_empty()) and Str.join_with(left, "\n").contains(want.why), "${name}: left because ${want.why}"))
	}
	file = Text.basename(asked.plant_file).drop_suffix(".roc")
	want_path = "${plants}/${file}.want"
	has_want = Shale.exists!(want_path)
	if has_want and !(want.count) {
		$checks = $checks.append((Compiler.read_text!(asked.path) == Compiler.read_text!(want_path), "${name}: comes out as ${file}.want"))
	}
	# In Typed, the types turn one call and leave the other, so the trial
	# decides neither.
	kinds = got.tally.kind_counts
	if name == "Typed" {
		count_of = |kind| kinds.find_first(|(named, _)| named == kind).map_ok(|(_, count)| count) ?? 0
		typed_held = kinds.len() == 2 and count_of("another type") == 1 and count_of("the module's type") == 1
		$checks = $checks.append((typed_held, "${name}: by type, ${json_of(kinds)}"))
	}
	confirms = got.tally.confirms
	$checks = $checks.append((confirms == want.confirms, "${name}: ${confirms.to_str()} confirm(s), want ${want.confirms.to_str()}"))
	match want.rounds {
		Rounds(rounds) => {
			got_rounds = got.tally.rounds
			$checks = $checks.append((got_rounds == rounds, "${name}: ${got_rounds.to_str()} round(s), want ${rounds.to_str()}"))
		}
		AnyRounds => {}
	}
	runs = got.tally.typed_roots.len()
	if !(want.types) {
		$checks = $checks.append((kinds.is_empty() and runs == 0, "${name}: under types=0, no roc-types run"))
	}
	# With the types, roc-types runs once per root when a candidate is open,
	# and not at all on a file with none (`no_types_run`).
	if want.types {
		want_runs = if want.no_types_run {
			0
		} else {
			asked.roots.len()
		}
		$checks = $checks.append((runs == want_runs, "${name}: ${runs.to_str()} roc-types run(s), want ${want_runs.to_str()}"))
	}
	if want.left > 0 and !(has_want) {
		$checks = $checks.append((Compiler.read_text!(asked.path) == Compiler.read_text!(asked.plant_file), "${name}: put back as it was"))
	}
	if want.beside != "" {
		beside = asked.paths.get(1) ?? ""
		$checks = $checks.append((Compiler.read_text!(beside) == Compiler.read_text!("${plants}/${want.beside}.roc"), "${name}: ${want.beside} left as it was"))
	}
	$checks
}

## The kinds' counts as Shale's `json_write` writes a map: `{"kind":1,...}`,
## in the order each was first counted.
json_of : List((Str, U64)) -> Str
json_of = |kinds| {
	members = kinds.map(|(kind, count)| "\"${kind}\":${count.to_str()}")
	"{${Str.join_with(members, ",")}}"
}
