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
	## edits it made.
	rewrite! : List(Str), Str => U64
	rewrite! = |paths, roc| {
		$made : U64
		var $made = 0
		var $changed = Bool.True
		while $changed {
			$changed = Bool.False
			for check in Checks.names {
				count = rewrite_check!(paths, check, roc)
				if count > 0 {
					$made = $made + count
					$changed = Bool.True
				}
			}
		}
		$made
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
		Shale.real!(roc)
	}
}

## Makes one kind of edit across `paths`, and answers how many it made.
rewrite_check! : List(Str), Str, Str => U64
rewrite_check! = |paths, check, roc| {
	$made : U64
	var $made = 0
	var $touched = []
	for file in Shale.roc_tree!(paths) {
		if file.err == "" {
			count = rewrite_file!({ path: file.path, nodes: file.nodes }, check)
			if count > 0 {
				$made = $made + count
				$touched = $touched.append(file.path)
			}
		}
	}
	if !($touched.is_empty()) {
		format!($touched, roc)
	}
	$made
}

## One kind of edit in one file, and how many it made. A finding inside
## another's span of the same kind is safe: its brackets are its own.
rewrite_file! : { path : Str, nodes : List(Tree.Node) }, Str => U64
rewrite_file! = |file, check| {
	text = Report.read_text!(file.path)
	source_lines = text.split_on("\n")
	findings = Checks.findings_in(file.nodes, source_lines)
	bytes = text.to_utf8()
	source = { path: file.path, text: bytes, nodes: file.nodes }
	match Edits.for_check(source, findings, check) {
		Err(why) => {
			Shale.fail!(why)
			0
		}
		Ok([]) => 0
		Ok(edits) =>
			match Edits.applied(bytes, edits) {
				Err(why) => {
					Shale.fail!(why)
					0
				}
				Ok(rewritten) => {
					Shale.write!(file.path, Str.from_utf8_lossy(rewritten))
					edits.len()
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
