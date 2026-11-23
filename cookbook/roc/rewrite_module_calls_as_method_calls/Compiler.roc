## The tree's own Roc, which judges each rewrite: `roc check` and its errors,
## `roc test`'s summary, and `roc fmt`. The Roc is the one the tree pins
## (its `roc-version`), since method lookup has changed
## between nightlies. Each refusal is worded as the Shale original's.
import pf.Shale
import Candidates
import lib.Text

Compiler := [].{

	## What `roc check` said: whether it passed, each error it reported (a
	## warning counts, since a warning fails the build), and its output.
	Check : { ok : Bool, errors : List(Candidates.CheckError), out : Str }

	## The OS path of the tree's Roc: the pin in `roc-version` where the run
	## starts, or in a self-test the pin pinned.shale names.
	roc_bin! : Bool => Str
	roc_bin! = |self_test| {
		pin = if self_test {
			Compiler.self_test_pin!({})
		} else if Shale.exists!("roc-version") {
			Text.trim(Compiler.read_text!("roc-version"))
		} else {
			Compiler.stop!("rewrite_module_calls_as_method_calls: no roc-version where the run starts: run it from the repository's root, whose roc-version pins its Roc")
		}
		roc = "tools:roc-${pin}/roc"
		if !(Shale.exists!(roc)) {
			Compiler.stop!("rewrite_module_calls_as_method_calls: no ${roc}: install Roc ${pin} into ~/tools/roc-${pin}/ (roc-apps's toolchain release carries it)")
		} else {
			real = Shale.real!(roc)
			said = Shale.run!({ argv: [real, "version"], keep: Whole(4000), timeout_ms: 30000, want: "" })
			if Text.trim(said.out) == "Roc compiler version ${pin}" {
				real
			} else {
				Compiler.stop!("rewrite_module_calls_as_method_calls: ${roc} says ${Text.trim(said.out)}, not Roc compiler version ${pin}")
			}
		}
	}

	## The pin every recipe's self-test runs, from cookbook/roc/pinned.shale's
	## `SELF_TEST_PIN`, its one home.
	self_test_pin! : {} => Str
	self_test_pin! = |{}| {
		pinned = Compiler.read_text!("cookbook:roc/pinned.shale")
		match Shale.re_find!(pinned, "\nconst SELF_TEST_PIN = \"([^\"]+)\"") {
			Found(matched) => matched.groups.first() ?? ""
			NoMatch => Compiler.stop!("rewrite_module_calls_as_method_calls: cookbook/roc/pinned.shale names no SELF_TEST_PIN")
		}
	}

	## `roc check` on the app `root`.
	check! : Str, Str => Compiler.Check
	check! = |roc, root| {
		ran = roc_run!([roc, "check", Shale.real!(root)], Whole(64000000), 900000)
		var $errors = []
		for said in Text.lines(ran.out) {
			# An error's heading is `── ✗ <title> ───── <path>:<line>:<col>`, and
			# a warning's has `●` in place of `✗`.
			if said.starts_with("── ✗ ") or said.starts_with("── ● ") {
				match Shale.re_find!(said, " ([^ ]+):([0-9]+):([0-9]+)$") {
					Found(at) => {
						heading = Str.from_utf8_lossy(said.to_utf8().drop_first(11))
						title = Text.trim(Text.before_first(heading, " ─"))
						place = at.groups.get(0) ?? ""
						line = U64.from_str(at.groups.get(1) ?? "") ?? 0
						$errors = $errors.append({ title, file: Text.basename(place), line })
					}
					NoMatch => {}
				}
			}
		}
		{ ok: ran.code == 0, errors: $errors, out: ran.out }
	}

	## The last line of `roc test` on `root`, without its time, and the exit
	## code: what a rewrite may not change.
	test_summary! : Str, Str => Str
	test_summary! = |roc, root| {
		ran = roc_run!([roc, "test", Shale.real!(root)], Whole(64000000), 900000)
		last = Text.lines(Text.trim(ran.out)).last() ?? ""
		summary = match Shale.re_find!(last, "^(.*) in [0-9.]+ ms\\.") {
			Found(matched) => matched.groups.first() ?? ""
			NoMatch => last
		}
		"${summary} (exit ${ran.code.to_str()})"
	}

	## Whether `roc fmt` finds the file as it would lay it out.
	is_fmt_clean! : Str, Str => Bool
	is_fmt_clean! = |roc, path| roc_run!([roc, "fmt", "--check", Shale.real!(path)], Tail(20), 120000).code == 0

	## The file laid out by `roc fmt`.
	fmt! : Str, Str => {}
	fmt! = |roc, path| {
		_ran = roc_run!([roc, "fmt", Shale.real!(path)], Tail(20), 120000)
		{}
	}

	## The first non-blank line of `out`, for a failure that names no error
	## (a crash).
	first_line_said : Str -> Str
	first_line_said = |out|
		match Text.lines(out).find_first(|said| Text.trim(said) != "") {
			Ok(said) => Text.trim(said)
			Err(_) => "nothing said"
		}

	## A file's text; a missing one stops the run.
	read_text! : Str => Str
	read_text! = |path|
		match Shale.read!(path) {
			Found(text) => text
			Missing => Compiler.stop!("rewrite_module_calls_as_method_calls: no ${path}")
		}

	## The run stopped, saying why; the text is never used.
	stop! : Str => Str
	stop! = |why| {
		Shale.fail!(why)
		""
	}
}

roc_run! : List(Str), [Head(U64), Tail(U64), Whole(U64)], U64 => { code : I64, out : Str }
roc_run! = |argv, keep, timeout_ms| Shale.run!({ argv, keep, timeout_ms, want: "" })
