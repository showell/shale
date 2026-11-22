## `write=1`: the files rewritten, one check a pass (Edits), each pass's
## files put through `roc fmt`, until a pass changes nothing, because taking
## off one pair of brackets can show the next. The Roc is the tree's own
## (cookbook/roc/pinned.shale), run by its OS path.
import pf.Shale
import Checks
import Edits
import Report
import lib.Text
import lib.Tree

Rewriting := [].{

	## Rewrites `paths` until a pass changes nothing, and answers how many
	## edits it made and what it knows of each file it read last: `settled`,
	## `""` for a file with no finding, else why roc-ast refused it
	## (Report.make!'s `settled`). Such a file is read no more, since no edit
	## will touch it, so a large tree is parsed whole once, not four times a
	## pass, which Roc's arena (it frees nothing) would not hold.
	rewrite! : List(Str), Str => { made : U64, settled : Dict(Str, Str) }
	rewrite! = |paths, roc| {
		$made : U64
		var $made = 0
		var $live = paths
		var $settled = Dict.empty()
		var $changed = Bool.True
		while $changed {
			$changed = Bool.False
			for check in Checks.names {
				pass = rewrite_check!($live, check, roc)
				$live = pass.live
				for (path, verdict) in pass.settled {
					$settled = $settled.insert(path, verdict)
				}
				if pass.made > 0 {
					$made = $made + pass.made
					$changed = Bool.True
				}
			}
		}
		{ made: $made, settled: $settled }
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
				Shale.fail!("find_extra_parens: no roc-version where the run starts: run it from the repository's root, whose roc-version pins its Roc")
			}
			Text.trim(Report.read_text!("roc-version"))
		}
		roc = "tools:roc-${pin}/roc"
		if !(Shale.exists!(roc)) {
			Shale.fail!("find_extra_parens: no ${roc}: install Roc ${pin} into ~/tools/roc-${pin}/ (roc-apps's toolchain release carries it)")
		}
		real = Shale.real!(roc)
		said = Shale.run!({ argv: [real, "version"], keep: Whole(4000), timeout_ms: 30000, want: "" })
		if Text.trim(said.out) != "Roc compiler version ${pin}" {
			Shale.fail!("find_extra_parens: ${roc} says ${Text.trim(said.out)}, not Roc compiler version ${pin}")
		}
		real
	}
}

## Makes one kind of edit across `paths`, and answers how many it made, which
## files had a finding of any check (the files still worth reading) and what
## is settled of the rest, each `(path, verdict)` as `rewrite!` keeps it.
rewrite_check! : List(Str), Str, Str => { made : U64, live : List(Str), settled : List((Str, Str)) }
rewrite_check! = |paths, check, roc| {
	$made : U64
	var $made = 0
	var $touched = []
	var $live = []
	var $settled = []
	if !(paths.is_empty()) {
		for file in Shale.roc_tree!(paths) {
			if file.err == "" {
				done = rewrite_file!({ path: file.path, nodes: file.nodes }, check)
				if done.found_any {
					$live = $live.append(file.path)
				} else {
					$settled = $settled.append((file.path, ""))
				}
				if done.made > 0 {
					$made = $made + done.made
					$touched = $touched.append(file.path)
				}
			} else {
				$settled = $settled.append((file.path, Report.refusal_of(file.path, file.err)))
			}
		}
	}
	if !($touched.is_empty()) {
		format!($touched, roc)
	}
	{ made: $made, live: $live, settled: $settled }
}

## One kind of edit in one file: how many it made, and whether the file had a
## finding of any check. A finding inside another's span of the same kind is
## safe: its brackets are its own.
rewrite_file! : { path : Str, nodes : List(Tree.Node) }, Str => { made : U64, found_any : Bool }
rewrite_file! = |file, check| {
	text = Report.read_text!(file.path)
	source_lines = text.split_on("\n")
	findings = Checks.findings_in(file.nodes, source_lines)
	bytes = text.to_utf8()
	source = { path: file.path, text: bytes, nodes: file.nodes }
	found_any = !(findings.is_empty())
	match Edits.for_check(source, findings, check) {
		Err(why) => {
			Shale.fail!(why)
			{ made: 0, found_any }
		}
		Ok([]) => { made: 0, found_any }
		Ok(edits) =>
			match Edits.applied(bytes, edits) {
				Err(why) => {
					Shale.fail!(why)
					{ made: 0, found_any }
				}
				Ok(rewritten) => {
					Shale.write!(file.path, Str.from_utf8_lossy(rewritten))
					{ made: edits.len(), found_any }
				}
			}
	}
}

## The files put through the tree's own formatter.
format! : List(Str), Str => {}
format! = |paths, roc| {
	var $argv = [roc, "fmt"]
	for path in paths {
		$argv = $argv.append(Shale.real!(path))
	}
	result = Shale.run!({
		argv: $argv,
		keep: Tail(20),
		timeout_ms: 120000,
		want: "roc-pin: the tree's own formatter",
	})
	if result.code != 0 {
		Shale.print!(result.out)
		Shale.fail!("find_extra_parens: `${roc} fmt` exited ${result.code.to_str()}")
	}
}

## pinned.shale's `SELF_TEST_PIN`, the one home of the self-tests' Roc.
self_test_pin! : {} => Str
self_test_pin! = |{}| {
	pinned = "cookbook:roc/pinned.shale"
	match Shale.re_find!(Report.read_text!(pinned), "\nconst SELF_TEST_PIN = \"([^\"]+)\"") {
		Found(found) =>
			match found.groups {
				[pin] => pin
				_ => {
					Shale.fail!("find_extra_parens: no SELF_TEST_PIN in ${pinned}")
					""
				}
			}
		NoMatch => {
			Shale.fail!("find_extra_parens: no SELF_TEST_PIN in ${pinned}")
			""
		}
	}
}
