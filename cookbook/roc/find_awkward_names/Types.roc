## The effects of a recipe's `types=1`: the tree's Roc found, and roc-types
## (Roc's own checker, https://github.com/showell/roc-types) run on each app
## of `root=`, its pin held to the tree's Roc. Reading and joining its output
## is cookbook/roc/lib/'s RocTypes; this file is the same in check_how_failure_is_reported,
## find_awkward_names and find_functions_and_parameters_that_dont_earn_their_keep, since a package reaches no effect.
import pf.Shale
import lib.RocTypes
import lib.Text

Types := [].{

	## The OS path of the tree's Roc: the pin in `roc-version` where the run
	## starts, or the self-test's pin, which cookbook/roc/pinned.shale names.
	## `who` names the recipe in a refusal.
	roc_bin! : Str, Bool => Str
	roc_bin! = |who, self_test| {
		pin = if self_test {
			pinned = read_text!(who, "cookbook:roc/pinned.shale")
			must!(RocTypes.self_test_pin(pinned).map_err(|_| "${who}: cookbook/roc/pinned.shale names no SELF_TEST_PIN"))
		} else if Shale.exists!("roc-version") {
			Text.trim(read_text!(who, "roc-version"))
		} else {
			must!(Err("${who}: no roc-version where the run starts: run it from the repository's root, whose roc-version pins its Roc"))
		}
		roc = RocTypes.roc_of_pin(pin)
		if !(Shale.exists!(roc)) {
			must!(Err("${who}: no ${roc}: install Roc ${pin} into ~/tools/roc-${pin}/ (roc-apps's toolchain release carries it)"))
		}
		real = Shale.real!(roc)
		said = Shale.run!({ argv: [real, "version"], keep: Whole(4000), timeout_ms: 30000, want: "" })
		if Text.trim(said.out) != "Roc compiler version ${pin}" {
			must!(Err("${who}: ${roc} says ${Text.trim(said.out)}, not Roc compiler version ${pin}"))
		}
		real
	}

	## The types of the nodes of `kind` in the files `paths`, from roc-types
	## run on each app of `root` with the Roc at `roc`, and a line for each
	## file no app reaches, except those in `exempt`.
	gather! : { who : Str, kind : RocTypes.Kind, root : Str, roc : Str, paths : List(Str), exempt : Set(Str) } => { typing : RocTypes.Typing, unreached : List(Str) }
	gather! = |asked| {
		roots = must!(RocTypes.root_apps(asked.who, asked.root))
		var $by_module = Dict.empty()
		for root in roots {
			got = modules_of!(root, asked.roc)
			$by_module = RocTypes.joined($by_module, got, asked.kind)
			for warning in got.warnings {
				Shale.print!("${asked.who}: roc-types ${root}: ${warning}")
			}
		}
		var $named = []
		for path in asked.paths {
			$named = $named.append({ path, real: Shale.real!(path) })
		}
		RocTypes.typing_of({ by_module: $by_module, named: $named, exempt: asked.exempt, roots })
	}
}

## The modules roc-types reports for the app `root`, built with the Roc at
## `roc`, or a stop: roc-types missing, of another format, built for another
## Roc, or refusing the app.
modules_of! : Str, Str => RocTypes.Modules
modules_of! = |root, roc| {
	pinned = RocTypes.pinned_checker(roc) ?? RocTypes.default_checker
	exe = if Shale.exists!(pinned) {
		pinned
	} else {
		RocTypes.default_checker
	}
	if !(Shale.exists!(exe)) {
		Shale.fail!("`roc_types`: no ${exe}: ${RocTypes.install_hint}")
	}
	ran = Shale.run!({
		argv: [Shale.real!(exe), Shale.real!(root)],
		keep: Whole(64000000),
		timeout_ms: 300000,
		want: "",
	})
	output_lines = Text.lines(ran.out)
	pin = must!(RocTypes.pin_of(output_lines, root))
	own = RocTypes.roc_of_pin(pin)
	if !(Shale.exists!(own)) or Shale.real!(own) != roc {
		Shale.fail!(RocTypes.another_compiler(pin, root, roc))
	}
	must!(RocTypes.modules_ran(output_lines, ran.code, root))
}

read_text! : Str, Str => Str
read_text! = |who, path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => must!(Err("${who}: no ${path}"))
	}

## What `tried` holds, or the run stopped with its refusal.
must! : Try(a, Str) => a
must! = |tried|
	match tried {
		Ok(value) => value
		Err(why) => {
			Shale.fail!(why)
			crash "Types: Shale.fail! returned"
		}
	}
