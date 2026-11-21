## A project's table of the floats that stay floats (`table=`), read: each
## line `Name: reason`, a name a module or what is in it (`Pixels`,
## `Scene.fade`, `Animation.Spot.lift`). The longest line that fits a
## finding answers for it, and a line no finding needs, or one naming a
## module no file below the table's folder holds, is a finding of its own,
## `unused_exception`.
import pf.Shale
import lib.Finding
import lib.Text
import lib.Tree

Exceptions := { path : Str, excuses : List(Exceptions.Excuse) }.{

	## A table's line, excusing a name's floats: the name, the module that name
	## starts with, and the line's number.
	Excuse : {
		name : Str,
		module_name : Str,
		line : U64,
	}

	## The table at `path`, empty when `path` is `""`. A missing table, a line
	## with no reason and a name listed twice each stop the run.
	read! : Str => Exceptions
	read! = |path| {
		if path == "" {
			return { path, excuses: [] }
		}
		if !(Shale.exists!(path)) {
			Shale.fail!("find_bare_floats: no table ${path}")
		}
		text = match Shale.read!(path) {
			Found(found) => found
			Missing => {
				Shale.fail!("find_bare_floats: no table ${path}")
				""
			}
		}
		var $excuses = []
		$number : U64
		var $number = 0
		for raw in Text.lines(text) {
			$number = $number + 1
			line = Text.trim(raw)
			if line != "" and !(line.starts_with("#")) {
				line_place = "find_bare_floats: ${path}:${$number.to_str()}"
				name = match Shale.re_find!(line, name_pattern) {
					Found(found) => excused_name(found.groups)
					NoMatch => {
						Shale.fail!("${line_place}: expected `Name: reason`")
						""
					}
				}
				if $excuses.any(|excuse| excuse.name == name) {
					Shale.fail!("${line_place}: `${name}` is listed twice")
				}
				module_name = name.split_on(".").first() ?? name
				$excuses = $excuses.append({ name, module_name, line: $number })
			}
		}
		{ path, excuses: $excuses }
	}

	## The longest name of the table that covers `key`, a dotted name: `key`
	## itself, or the part of it before one of its dots.
	cover : Exceptions, Str -> Try(Str, [Uncovered])
	cover = |exceptions, key| {
		parts = key.split_on(".")
		var $count = parts.len()
		while $count >= 1 {
			name = Str.join_with(parts.take_first($count), ".")
			if exceptions.excuses.any(|excuse| excuse.name == name) {
				return Ok(name)
			}
			$count = $count - 1
		}
		Err(Uncovered)
	}

	## The table's lines that excuse nothing: each naming a module no file
	## below the table's folder holds, or, for a module the run read, a name
	## no finding needed (`used`). A line for a module the run didn't read
	## isn't judged.
	find_unused! : Exceptions, { read_modules : Set(Str), used : Set(Str) } => List(Finding)
	find_unused! = |exceptions, run| {
		if exceptions.excuses.is_empty() {
			return []
		}
		below = Text.path_under(Text.dirname(exceptions.path), "**/*.roc")
		known = Set.from_list(Shale.glob!(below).map(Tree.module_name))
		finding_at = |excuse, what| {
			path: exceptions.path,
			line: excuse.line,
			check: "unused_exception",
			what: "`${excuse.name}` ${what}",
		}
		var $found = []
		for excuse in exceptions.excuses {
			is_known = known.contains(excuse.module_name)
			is_needed = run.used.contains(excuse.name)
			is_read = run.read_modules.contains(excuse.module_name)
			if !is_known {
				$found = $found.append(finding_at(excuse, "names a module no file here holds"))
			}
			if is_known and is_read and !is_needed {
				$found = $found.append(finding_at(excuse, "excuses nothing: no finding needs it"))
			}
		}
		$found
	}
}

## A table's line: a name of letters, digits and `_`, its parts joined by
## `.`, a part after the first perhaps ending in `!`, then `:` and a reason.
name_pattern : Str
name_pattern = "^([A-Za-z][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*!?)*): +[^ ]"

## The name a match of `name_pattern` holds, its one group.
excused_name : List(Str) -> Str
excused_name = |groups|
	match groups.first() {
		Ok(name) => name
		Err(_) => crash "find_bare_floats: the table's pattern has a group, the name"
	}
