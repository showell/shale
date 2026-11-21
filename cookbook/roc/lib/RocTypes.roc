## roc-types' output read: the type Roc's own checker gives each expression
## and pattern of an app's modules, by region, in version 1 of the format
## (https://github.com/showell/roc-types, its FORMAT.md), as
## cookbook/roc/types.shale reads it, refusals worded alike. Running
## roc-types, and holding its pin to the tree's Roc, are effects, so they
## stay in the recipe that asks.
import Text

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
		said = words(head)
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
	span_of : { line : U64, col : U64, end_line : U64, end_col : U64 } -> Str
	span_of = |region| {
		starts = "${region.line.to_str()}:${region.col.to_str()}"
		"${starts}:${region.end_line.to_str()}:${region.end_col.to_str()}"
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
}

## The version of the format this reads.
format : Str
format = "1"

## A line's words, as Shale's `words` splits it: at each run of spaces, tabs,
## carriage returns and newlines.
words : Str -> List(Str)
words = |text| {
	var $said = []
	var $word = []
	for byte in text.to_utf8() {
		if byte == ' ' or byte == '\t' or byte == '\r' or byte == '\n' {
			$said = with_word($said, $word)
			$word = []
		} else {
			$word = $word.append(byte)
		}
	}
	with_word($said, $word)
}

## `said` with `bytes` after it as a word, when there are any.
with_word : List(Str), List(U8) -> List(Str)
with_word = |said, bytes|
	if bytes.is_empty() {
		said
	} else {
		said.append(Str.from_utf8_lossy(bytes))
	}

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
