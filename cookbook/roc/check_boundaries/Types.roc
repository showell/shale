## The types check_boundaries' `bool_param` reads with `types=1`: roc-types (Roc's own checker,
## https://github.com/showell/roc-types) run on each app of `root=`, its pin
## held to the tree's Roc, and its output read by cookbook/roc/lib/'s
## RocTypes, as cookbook/roc/types.shale's `roc_types_for_files` gathers
## them; and the tree's Roc, as cookbook/roc/pinned.shale's `roc_bin` finds
## it. Only the patterns' types are kept: a definition's and a parameter's
## type is its pattern's. Each refusal is worded as the Shale libraries word
## it.
import pf.Shale
import lib.RocTypes
import lib.Text
import lib.Tree

Types := [].{

	## The checker's type of each pattern of each file some root reaches: the
	## file's real path, by the path the run names it by, and the types by
	## real path, then by the pattern's span.
	Typing : {
		real : Dict(Str, Str),
		patterns : Dict(Str, Dict(Str, Str)),
	}

	## No types, for a run without them.
	none : Types.Typing
	none = { real: Dict.empty(), patterns: Dict.empty() }

	## The OS path of the tree's Roc: the pin in `roc-version` where the run
	## starts, or the self-test's pin, which cookbook/roc/pinned.shale names.
	roc_bin! : Bool => Str
	roc_bin! = |self_test| {
		pin = if self_test {
			self_test_pin!({})
		} else if Shale.exists!("roc-version") {
			Text.trim(read_text!("roc-version"))
		} else {
			stop!("${who}: no roc-version where the run starts: run it from the repository's root, whose roc-version pins its Roc")
		}
		roc = "tools:roc-${pin}/roc"
		if !(Shale.exists!(roc)) {
			stop!("${who}: no ${roc}: install Roc ${pin} into ~/tools/roc-${pin}/ (roc-apps's toolchain release carries it)")
		} else {
			Shale.real!(roc)
		}
	}

	## The types of the files `paths`, from roc-types run on each app of
	## `root`, with the Roc at `roc`; and a line for each file no app
	## reaches, except those in `exempt`. Where two apps reach a module, the
	## first app's types are kept, and where patterns share a region, the
	## outermost's.
	gather! : { root : Str, roc : Str, paths : List(Str), exempt : Set(Str) } => { typing : Types.Typing, unreached : List(Str) }
	gather! = |asked| {
		roots = match RocTypes.root_apps(who, asked.root) {
			Ok(apps) => apps
			Err(why) => {
				Shale.fail!(why)
				[]
			}
		}
		var $patterns = Dict.empty()
		for root in roots {
			got = modules_of!(root, asked.roc)
			for path in got.paths {
				if !($patterns.contains(path)) {
					var $by_span = Dict.empty()
					for record in got.records.get(path) ?? [] {
						if record.kind == "pat" and !($by_span.contains(record.span)) {
							$by_span = $by_span.insert(record.span, record.written)
						}
					}
					$patterns = $patterns.insert(path, $by_span)
				}
			}
			for warning in got.warnings {
				Shale.print!("${who}: roc-types ${root}: ${warning}")
			}
		}
		var $real = Dict.empty()
		var $unreached = []
		for path in asked.paths {
			real = Shale.real!(path)
			if $patterns.contains(real) {
				$real = $real.insert(path, real)
			} else if !(asked.exempt.contains(path)) {
				$unreached = $unreached.append("not reached by ${Str.join_with(roots, ",")}: ${path}")
			}
		}
		{ typing: { real: $real, patterns: $patterns }, unreached: $unreached }
	}

	## The checker's type of the pattern `node` in the file at `path`
	## (types.shale's `roc_type_at` of a `pat`), when a root reached the file
	## and the checker typed that region.
	pattern_type : Types.Typing, Str, Tree.Node -> Try(Str, [NoType])
	pattern_type = |typing, path, node| {
		span = RocTypes.span_of({ line: node.line, column: node.column, end_line: node.end_line, end_column: node.end_column })
		match typing.real.get(path) {
			Ok(real) => {
				by_span = typing.patterns.get(real) ?? Dict.empty()
				match by_span.get(span) {
					Ok(written) => Ok(written)
					Err(_) => Err(NoType)
				}
			}
			Err(_) => Err(NoType)
		}
	}
}

## The recipe, as its refusals name it.
who : Str
who = "check_boundaries"

## The run stopped, saying why; the text is never used.
stop! : Str => Str
stop! = |why| {
	Shale.fail!(why)
	""
}

read_text! : Str => Str
read_text! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => stop!("${who}: no ${path}")
	}

## The pin every recipe's self-test runs, from cookbook/roc/pinned.shale's
## `SELF_TEST_PIN`, its one home.
self_test_pin! : {} => Str
self_test_pin! = |{}| {
	pinned = read_text!("cookbook:roc/pinned.shale")
	match Shale.re_find!(pinned, "\nconst SELF_TEST_PIN = \"([^\"]+)\"") {
		Found(matched) => matched.groups.first() ?? ""
		NoMatch => stop!("${who}: cookbook/roc/pinned.shale names no SELF_TEST_PIN")
	}
}

## The modules roc-types reports for the app `root`, built with the Roc at
## `roc`, or a stop: roc-types missing, of another format, built for another
## Roc, or refusing the app.
modules_of! : Str, Str => RocTypes.Modules
modules_of! = |root, roc| {
	exe = checker_for!(roc)
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
	pin = match RocTypes.pin_of(output_lines, root) {
		Ok(found) => found
		Err(why) => stop!(why)
	}
	own = "tools:roc-${pin}/roc"
	if !(Shale.exists!(own)) or Shale.real!(own) != roc {
		another = "its types would be another compiler's. Install the roc-types of the tree's pin (https://github.com/showell/roc-types), or move the tree to ${pin}"
		Shale.fail!("`roc_types`: roc-types reads Roc ${pin}, and ${root}'s tree is built with ${roc}: ${another}")
	}
	if ran.code != 0 {
		ended = if ran.code < 0 {
			"did not exit"
		} else {
			"exited ${ran.code.to_str()}"
		}
		Shale.fail!("`roc_types`: roc-types ${root} ${ended}, what it said:\n${RocTypes.said_on_failure(output_lines)}")
	}
	match RocTypes.modules_of(output_lines) {
		Ok(modules) => modules
		Err(why) => {
			Shale.fail!(why)
			{ paths: [], records: Dict.empty(), warnings: [] }
		}
	}
}

## The checker for the compiler at `roc`: the one installed for its pin,
## `tools:roc-types-<pin>/bin/roc-types`, as compilers sit side by side, or
## else the default.
checker_for! : Str => Str
checker_for! = |roc| {
	default = "tools:roc-types/bin/roc-types"
	match Shale.re_find!(roc, "/roc-([^/]+)/roc$") {
		Found(matched) => {
			own = "tools:roc-types-${matched.groups.first() ?? ""}/bin/roc-types"
			if Shale.exists!(own) {
				own
			} else {
				default
			}
		}
		NoMatch => default
	}
}
