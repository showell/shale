## Every expression's type in an app, from roc-types (Roc's own checker,
## https://github.com/showell/roc-types) run on the root with its pin held
## to the tree's Roc, or read from a file a gate kept, and read by
## cookbook/roc/lib/'s RocTypes. Each refusal is worded as the Shale
## original's.
import pf.Shale
import Compiler
import lib.RocTypes
import lib.Text

Types := [].{

	## An app's types: each expression's type keyed by `<real path>|<region>`
	## (where several share a region, the first roc-types gives, the
	## outermost), and the real paths of the modules it reaches.
	Typed : { by_region : Dict(Str, Str), modules : Set(Str) }

	## The types of the app `root`, from roc-types run with the Roc at `roc`,
	## or from `types_from` when it names a file (its records kept for the
	## modules in `only` alone). roc-types' warnings are printed.
	of_root! : { root : Str, roc : Str, types_from : Str, only : Set(Str) } => Types.Typed
	of_root! = |asked| {
		got = if asked.types_from == "" {
			from_run!(asked.root, asked.roc)
		} else {
			from_file!(asked.types_from, asked.root, asked.roc, asked.only)
		}
		var $by_region = Dict.empty()
		for path in got.paths {
			for record in got.records.get(path) ?? [] {
				key = "${path}|${record.span}"
				if record.kind == "expr" and !($by_region.contains(key)) {
					$by_region = $by_region.insert(key, record.written)
				}
			}
		}
		for warning in got.warnings {
			Shale.print!("rewrite_method_calls: roc-types ${asked.root}: ${warning}")
		}
		{ by_region: $by_region, modules: Set.from_list(got.paths) }
	}

	## The checker for the compiler at `roc`: the one installed for its pin,
	## `tools:roc-types-<pin>/bin/roc-types`, as compilers sit side by side,
	## or else the default.
	checker_for! : Str => Str
	checker_for! = |roc|
		match Shale.re_find!(roc, "/roc-([^/]+)/roc$") {
			Found(matched) => {
				own = "tools:roc-types-${matched.groups.first() ?? ""}/bin/roc-types"
				if Shale.exists!(own) {
					own
				} else {
					default_checker
				}
			}
			NoMatch => default_checker
		}

	## The default checker, which the self-test needs installed.
	default_checker : Str
	default_checker = "tools:roc-types/bin/roc-types"
}

## roc-types run on `root`: a stop when it is missing, of another format,
## built for another Roc than `roc`, or refusing the app.
from_run! : Str, Str => RocTypes.Modules
from_run! = |root, roc| {
	exe = Types.checker_for!(roc)
	if !(Shale.exists!(exe)) {
		Shale.fail!("`roc_types`: no ${exe}: ${RocTypes.install_hint}")
	}
	ran = Shale.run!({
		argv: [Shale.real!(exe), Shale.real!(root)],
		keep: Whole(64000000),
		timeout_ms: 300000,
		want: "",
	})
	modules_said!(Text.lines(ran.out), ran.code, root, roc)
}

## The types roc-types wrote of `root` to the file `path`, read with no
## program run: the records of the modules in `only` alone, every module's
## `== ` line kept, so the modules reached stay whole.
from_file! : Str, Str, Str, Set(Str) => RocTypes.Modules
from_file! = |path, root, roc, only| {
	if !(Shale.exists!(path)) {
		Shale.fail!("`roc_types`: no ${path}, the types of ${root} to read")
	}
	var $kept = []
	var $keeping = Bool.True
	for said in Text.lines(Compiler.read_text!(path)) {
		if said.starts_with("== ") {
			$keeping = only.contains(said.drop_prefix("== "))
			$kept = $kept.append(said)
		} else if $keeping or $kept.is_empty() {
			$kept = $kept.append(said)
		}
	}
	modules_said!($kept, 0, root, roc)
}

## roc-types' lines read, its pin held to `roc` first, since the errors (or
## the silence) of a checker from another Roc say nothing about this tree,
## then its exit `code`.
modules_said! : List(Str), I64, Str, Str => RocTypes.Modules
modules_said! = |output_lines, code, root, roc| {
	pin = match RocTypes.pin_of(output_lines, root) {
		Ok(found) => found
		Err(why) => Compiler.stop!(why)
	}
	own = "tools:roc-${pin}/roc"
	if !(Shale.exists!(own)) or Shale.real!(own) != roc {
		another = "its types would be another compiler's. Install the roc-types of the tree's pin (https://github.com/showell/roc-types), or move the tree to ${pin}"
		Shale.fail!("`roc_types`: roc-types reads Roc ${pin}, and ${root}'s tree is built with ${roc}: ${another}")
	}
	if code != 0 {
		ended = if code < 0 {
			"did not exit"
		} else {
			"exited ${code.to_str()}"
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
