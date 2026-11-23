## roc-types' output read: the type Roc's own checker gives each expression
## and pattern of an app's modules, by region, in version 1 of the format
## (https://github.com/showell/roc-types, its FORMAT.md), refusals worded as
## the Shale libraries worded them; the types of several apps joined, and a
## node's type found; and the paths of the Roc and the roc-types a pin names.
## Running roc-types, and holding its pin to the tree's Roc, are effects, so
## they stay in the recipe that asks: a small Types.roc in each of
## check_how_failure_is_reported, find_awkward_names and find_functions_and_parameters_that_dont_earn_their_keep, the same file in all three.
import Text
import Tree

RocTypes := [].{

	## A type record of a module: `kind` is `expr` or `pat`, `tag` the
	## checker's name for the node, `span` its region as
	## `line:col:end_line:end_col` (RocTypes.span_of), and `written` the type
	## as the checker writes it, its escapes undone.
	Record : {
		kind : Str,
		tag : Str,
		span : Str,
		written : Str,
	}

	## The modules of an output: each module's real, absolute path in the
	## checker's order, its records in the checker's pre-order (where nodes
	## share a region, the outermost first), and the lines printed before the
	## first module.
	Modules : {
		paths : List(Str),
		records : Dict(Str, List(RocTypes.Record)),
		warnings : List(Str),
	}

	## How to install roc-types, for the refusals that find it missing or
	## another version.
	install_hint : Str
	install_hint = "`shale install.shale` in a checkout of https://github.com/showell/roc-types installs it"

	## The pin of the Roc roc-types was built from, out of the output's first
	## line, `roc-types 1 <pin>`, or why that line is not one. `root` is the
	## app the output is of, named in the refusal.
	pin_of : List(Str), Str -> Try(Str, Str)
	pin_of = |output_lines, root| {
		head = output_lines.first() ?? ""
		said = Text.words(head)
		if !(head.starts_with("roc-types ")) {
			Err("`roc_types`: no `roc-types ${format}` header (${root}, the first line `${head}`): an older roc-types, or not roc-types; ${RocTypes.install_hint}")
		} else if said.len() < 2 or (said.get(1) ?? "") != format {
			Err("`roc_types`: roc-types' header is `${head}`, not format ${format}'s, the one this reads; ${RocTypes.install_hint}")
		} else if said.len() != 3 {
			Err("`roc_types`: a header that is not `roc-types <N> <pin>`: ${head}")
		} else {
			Ok(said.get(2) ?? "")
		}
	}

	## What roc-types said when it failed: its lines after the header, the
	## first 60 of them and a count of the rest.
	said_on_failure : List(Str) -> Str
	said_on_failure = |output_lines| {
		after_head = output_lines.drop_first(1)
		shown = after_head.take_first(60)
		left = after_head.len() - shown.len()
		if left > 0 {
			Str.join_with(shown.append("(${left.to_str()} lines more)"), "\n")
		} else {
			Str.join_with(shown, "\n")
		}
	}

	## The modules of an output whose header was read, or why a line after it
	## is not of the format.
	modules_of : List(Str) -> Try(RocTypes.Modules, Str)
	modules_of = |output_lines| {
		var $paths = []
		var $warnings = []
		var $records = Dict.empty()
		var $current = []
		var $path = ""
		$number : U64
		var $number = 1
		for output_line in output_lines.drop_first(1) {
			$number = $number + 1
			if output_line.starts_with("== ") {
				if $path != "" {
					$records = $records.insert($path, $current)
				}
				$current = []
				$path = output_line.drop_prefix("== ")
				at = "line ${$number.to_str()}"
				if !($path.starts_with("/")) {
					return Err("`roc_types`: ${at}, a module path that is not absolute: ${$path}")
				}
				if $records.contains($path) {
					return Err("`roc_types`: ${at}, module ${$path} twice")
				}
				$records = $records.insert($path, [])
				$paths = $paths.append($path)
			} else if $path == "" {
				$warnings = $warnings.append(output_line)
			} else if output_line.starts_with("t\t") {
				$current = $current.append(record_of(output_line, $number)?)
			} else {
				return Err("`roc_types`: line ${$number.to_str()} is no record of roc-types' format: ${output_line}")
			}
		}
		if $path != "" {
			$records = $records.insert($path, $current)
		}
		Ok({ paths: $paths, records: $records, warnings: $warnings })
	}

	## A region as the records' `span` writes it, `line:col:end_line:end_col`,
	## for finding a node's type by the node's own region.
	span_of : { line : U64, column : U64, end_line : U64, end_column : U64 } -> Str
	span_of = |region| {
		starts = "${region.line.to_str()}:${region.column.to_str()}"
		"${starts}:${region.end_line.to_str()}:${region.end_column.to_str()}"
	}

	## The app paths of a recipe's `root=`, which may name several
	## (`root=a.roc,b.roc`), or why it names none. `who` names the recipe.
	root_apps : Str, Str -> Try(List(Str), Str)
	root_apps = |who, root| {
		apps = root.split_on(",").map(Text.trim).keep_if(|named| named != "")
		if apps.is_empty() {
			Err("${who}: types=1 needs root=, an app (or several, `root=a.roc,b.roc`) importing the files of the glob: roc-types reads an app, and a type module only through one")
		} else {
			Ok(apps)
		}
	}

	## The modules of an output whose pin was read, from a run of roc-types
	## on the app `root` that ended with `code`, or why they can't be had:
	## roc-types failed, or a line is not of the format.
	modules_ran : List(Str), I64, Str -> Try(RocTypes.Modules, Str)
	modules_ran = |output_lines, code, root|
		if code == 0 {
			modules_of(output_lines)
		} else {
			ended = if code < 0 {
				"did not exit"
			} else {
				"exited ${code.to_str()}"
			}
			Err("`roc_types`: roc-types ${root} ${ended}, what it said:\n${said_on_failure(output_lines)}")
		}

	## The refusal of a roc-types built from Roc `pin`, run on the app `root`
	## of a tree built with the Roc at `roc`.
	another_compiler : Str, Str, Str -> Str
	another_compiler = |pin, root, roc| {
		another = "its types would be another compiler's. Install the roc-types of the tree's pin (https://github.com/showell/roc-types), or move the tree to ${pin}"
		"`roc_types`: roc-types reads Roc ${pin}, and ${root}'s tree is built with ${roc}: ${another}"
	}

	## The types a run keeps: each file's real path, by the path the run
	## names it by, for the files some app reached; and the checker's type of
	## each node of the kind asked for, by its module's real path, then by its
	## region (RocTypes.span_of).
	Typing : {
		real : Dict(Str, Str),
		by_module : Dict(Str, Dict(Str, Str)),
	}

	## The nodes whose types a run keeps: the expressions (check_how_failure_is_reported'), or
	## the patterns, whose type is a definition's or a parameter's.
	Kind : [Expressions, Patterns]

	## No types, for a run without them.
	no_types : RocTypes.Typing
	no_types = { real: Dict.empty(), by_module: Dict.empty() }

	## The types of an app's modules added to those of the apps before it: a
	## module an earlier app reached keeps that app's types, and where nodes
	## share a region, the outermost's type is kept.
	joined : Dict(Str, Dict(Str, Str)), RocTypes.Modules, RocTypes.Kind -> Dict(Str, Dict(Str, Str))
	joined = |by_module, modules, kind| {
		wanted = match kind {
			Expressions => "expr"
			Patterns => "pat"
		}
		var $by_module = by_module
		for path in modules.paths {
			if !($by_module.contains(path)) {
				var $by_span = Dict.empty()
				for record in modules.records.get(path) ?? [] {
					if record.kind == wanted and !($by_span.contains(record.span)) {
						$by_span = $by_span.insert(record.span, record.written)
					}
				}
				$by_module = $by_module.insert(path, $by_span)
			}
		}
		$by_module
	}

	## The typing of the files a run names, each with its real path, from the
	## types the apps `roots` reached; and a line for each file no app
	## reached, except those in `exempt`.
	typing_of : { by_module : Dict(Str, Dict(Str, Str)), named : List({ path : Str, real : Str }), exempt : Set(Str), roots : List(Str) } -> { typing : RocTypes.Typing, unreached : List(Str) }
	typing_of = |gathered| {
		var $real = Dict.empty()
		var $unreached = []
		for file in gathered.named {
			if gathered.by_module.contains(file.real) {
				$real = $real.insert(file.path, file.real)
			} else if !(gathered.exempt.contains(file.path)) {
				$unreached = $unreached.append("not reached by ${Str.join_with(gathered.roots, ",")}: ${file.path}")
			}
		}
		{ typing: { real: $real, by_module: gathered.by_module }, unreached: $unreached }
	}

	## The checker's type of the node `node` of the file the run names
	## `path`, when an app reached the file and the checker typed that
	## region.
	type_at : RocTypes.Typing, Str, Tree.Node -> Try(Str, [NoType])
	type_at = |typing, path, node| {
		span = span_of({ line: node.line, column: node.column, end_line: node.end_line, end_column: node.end_column })
		match typing.real.get(path) {
			Ok(real) => {
				by_span = typing.by_module.get(real) ?? Dict.empty()
				match by_span.get(span) {
					Ok(written) => Ok(written)
					Err(_) => Err(NoType)
				}
			}
			Err(_) => Err(NoType)
		}
	}

	## The pin every recipe's self-test runs, out of the text of
	## cookbook/roc/pinned.shale, its `SELF_TEST_PIN` and the one home of it.
	self_test_pin : Str -> Try(Str, [NoPin])
	self_test_pin = |pinned| {
		for after in pinned.split_on("\nconst SELF_TEST_PIN = \"").drop_first(1) {
			quoted = after.split_on("\"")
			pin = quoted.first() ?? ""
			if quoted.len() > 1 and pin != "" {
				return Ok(pin)
			}
		}
		Err(NoPin)
	}

	## The Roc a pin names, as installed under the `tools` mount.
	roc_of_pin : Str -> Str
	roc_of_pin = |pin| "tools:roc-${pin}/roc"

	## The roc-types run when none is installed for the tree's pin.
	default_checker : Str
	default_checker = "tools:roc-types/bin/roc-types"

	## The roc-types installed for the pin of the Roc at the OS path `roc`
	## (`.../roc-<pin>/roc`), as compilers sit side by side, when that path
	## names a pin.
	pinned_checker : Str -> Try(Str, [NoPin])
	pinned_checker = |roc| {
		folders = roc.drop_suffix("/roc").split_on("/")
		folder = folders.last() ?? ""
		pin = folder.drop_prefix("roc-")
		if roc.ends_with("/roc") and folders.len() > 1 and folder.starts_with("roc-") and pin != "" {
			Ok("tools:roc-types-${pin}/bin/roc-types")
		} else {
			Err(NoPin)
		}
	}
}

## The version of the format this reads.
format : Str
format = "1"

## The record a `t` line holds, `t`, the kind, the tag, the span's four
## numbers and the type, tab-separated, or why it is not one. `number` is the
## line's, named in a refusal.
record_of : Str, U64 -> Try(RocTypes.Record, Str)
record_of = |output_line, number| {
	at = |{}| "line ${number.to_str()}"
	fields = output_line.split_on("\t")
	field = |place| fields.get(place - 1) ?? ""
	kind = field(2)
	if fields.len() != 8 {
		Err("`roc_types`: ${at({})}, a record of ${fields.len().to_str()} fields, not 8: ${output_line}")
	} else if kind != "expr" and kind != "pat" {
		Err("`roc_types`: ${at({})}, a record of kind `${kind}`")
	} else {
		var $span = ""
		for number_text in fields.sublist({ start: 3, len: 4 }) {
			match I64.from_str(number_text) {
				Ok(parsed) => {
					$span = if $span == "" {
						parsed.to_str()
					} else {
						"${$span}:${parsed.to_str()}"
					}
				}
				Err(_) => {
					return Err("`roc_types`: ${at({})}, a span's number `${number_text}`")
				}
			}
		}
		written = if field(8).contains("\\") {
			unescaped(field(8))?
		} else {
			field(8)
		}
		Ok({ kind, tag: field(3), span: $span, written })
	}
}

## A type's text with roc-types' escapes of `\`, tab, newline and carriage
## return undone, or why it holds a bad one.
unescaped : Str -> Try(Str, Str)
unescaped = |text| {
	bytes = text.to_utf8()
	var $out = []
	$at : U64
	var $at = 0
	while $at < bytes.len() {
		byte = bytes.get($at) ?? 0
		if byte == '\\' and $at + 1 < bytes.len() {
			escaped = bytes.get($at + 1) ?? 0
			plain = if escaped == 't' {
				Ok('\t')
			} else if escaped == 'n' {
				Ok('\n')
			} else if escaped == 'r' {
				Ok('\r')
			} else if escaped == '\\' {
				Ok('\\')
			} else {
				Err(BadEscape)
			}
			match plain {
				Ok(kept) => {
					$out = $out.append(kept)
				}
				Err(BadEscape) => {
					shown = Str.from_utf8_lossy([escaped])
					return Err("`roc_types`: a type with a bad escape, `\\${shown}`: ${text}")
				}
			}
			$at = $at + 2
		} else if byte == '\\' {
			return Err("`roc_types`: a type ending in a lone `\\`: ${text}")
		} else {
			$out = $out.append(byte)
			$at = $at + 1
		}
	}
	Ok(Str.from_utf8_lossy($out))
}
