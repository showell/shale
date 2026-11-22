## What hunt_names' `stale_doc_name` reads (the Shale original's
## doc_names.shale): the names a file's comment lines put in backticks, and whether each still
## names something in the file, the modules it imports, or the module a
## `Module.name` names. Only a name a reader can't take for a prose word is
## judged: one of two words or more (`two_opt`), or a module's member
## (`Route.two_opt`). A name cited from another language or library, or
## with a possessive before it, is another's. Reading a module beside the
## file that the run didn't parse is an effect, so this module has them.
import pf.Shale
import lib.Finding
import lib.Text
import lib.Tree

DocNames := [].{

	## What the run has read of names: the self-test's own (`known`), each
	## module's by its name, each file's by its path (`Unread` for one that
	## couldn't be), and the sources a citation names.
	Seen : {
		known : Set(Str),
		by_module : Dict(Str, Set(Str)),
		by_path : Dict(Str, [Read(Set(Str)), Unread]),
		sources : Set(Str),
	}

	## A parsed file, its lines and its tree.
	FileRead : { path : Str, nodes : List(Tree.Node), source_lines : List(Str) }

	## The names the parsed files hold, by module and by path.
	seen_of : List(DocNames.FileRead), Set(Str), Set(Str) -> DocNames.Seen
	seen_of = |files, known, sources| {
		var $by_module = Dict.empty()
		var $by_path = Dict.empty()
		for file in files {
			module_named = Tree.module_name(file.path)
			own = names_in_tree(file.nodes)
			$by_module = $by_module.insert(module_named, ($by_module.get(module_named) ?? Set.empty()).union(own))
			$by_path = $by_path.insert(file.path, Read(own))
		}
		{ known, by_module: $by_module, by_path: $by_path, sources }
	}

	## Each backticked name in a comment of each file that names nothing
	## there, once a line, file by file.
	findings! : List(DocNames.FileRead), DocNames.Seen => List(Finding)
	findings! = |files, seen_before| {
		var $seen = seen_before
		var $found = []
		for file in files {
			judged = file_findings!(file, $seen)
			$seen = judged.seen
			$found = $found.concat(judged.found)
		}
		$found
	}
}

## A backticked name: its line, its module (`""` for a bare name), the name,
## and whether a possessive or a cited source says it is another's.
DocName : { line : U64, module_part : Str, name : Str, is_others : Bool }

## The file's stale names, and what the run has read of names since.
file_findings! : DocNames.FileRead, DocNames.Seen => { found : List(Finding), seen : DocNames.Seen }
file_findings! = |file, seen_before| {
	var $seen = seen_before
	own = match seen_before.by_path.get(file.path) {
		Ok(Read(names)) => names
		_ => names_in_tree(file.nodes)
	}
	base = Text.basename(file.path)
	folder = Str.from_utf8_lossy(file.path.to_utf8().take_first(file.path.count_utf8_bytes() - base.count_utf8_bytes()))
	own_module = Tree.module_name(file.path)
	is_platform = file.nodes.any(|node| node.kind == "platform")
	var $imported = []
	var $all_imports_known = Bool.True
	for imported in imports_in_tree(file.nodes) {
		looked = module_names!(folder, imported, $seen)
		$seen = looked.seen
		match looked.names {
			Ok(names) => {
				$imported = $imported.append(names)
			}
			Err(Unknown) => {
				$all_imports_known = Bool.False
			}
		}
	}
	var $said = Set.empty()
	var $found = []
	for doc_name in doc_names_in(file.source_lines, $seen.sources) {
		name = doc_name.name
		plain = name.drop_suffix("!")
		holds = |names| names.contains(name) or names.contains(plain)
		is_host_symbol = is_platform and plain.starts_with("roc_")
		is_bare = doc_name.module_part == ""
		var $judged = !(is_bare and (doc_name.is_others or !($all_imports_known) or is_host_symbol))
		var $names_something = Bool.False
		if is_bare or doc_name.module_part == own_module {
			if holds(own) or $seen.known.contains(plain) {
				$names_something = Bool.True
			}
			if is_bare and $imported.any(holds) {
				$names_something = Bool.True
			}
		} else {
			looked = module_names!(folder, doc_name.module_part, $seen)
			$seen = looked.seen
			match looked.names {
				Ok(names) => {
					if holds(names) {
						$names_something = Bool.True
					}
				}
				Err(Unknown) => {
					$judged = Bool.False
				}
			}
		}
		shown = if is_bare {
			name
		} else {
			"${doc_name.module_part}.${name}"
		}
		key = "${doc_name.line.to_str()}|${shown}"
		if $judged and !($names_something) and !($said.contains(key)) {
			$said = $said.insert(key)
			place = if is_bare {
				"this file or its imports"
			} else {
				doc_name.module_part
			}
			what = "a comment names `${shown}`, which names nothing in ${place}: a rename left it behind, or it names what is gone"
			$found = $found.append({ path: file.path, line: doc_name.line, check: "stale_doc_name", what })
		}
	}
	{ found: $found, seen: $seen }
}

## The names module `module` holds, or `Unknown` when they can't be known
## (a builtin's module, a package's the run didn't read): those of
## `<folder><module>.roc` beside the file, read once, else those of the
## files read that hold a module of that name.
module_names! : Str, Str, DocNames.Seen => { names : Try(Set(Str), [Unknown]), seen : DocNames.Seen }
module_names! = |folder, module_named, seen| {
	path = "${folder}${module_named}.roc"
	looked_up = match seen.by_path.get(path) {
		Ok(known) => { read: known, after: seen }
		Err(_) => {
			read_now = if Shale.exists!(path) {
				match Shale.roc_tree!([path]) {
					[parsed, ..] if parsed.err == "" => Read(names_in_tree(parsed.nodes))
					_ => Unread
				}
			} else {
				Unread
			}
			{ read: read_now, after: { ..seen, by_path: seen.by_path.insert(path, read_now) } }
		}
	}
	read = looked_up.read
	after = looked_up.after
	names = match read {
		Read(names_read) => Ok(names_read)
		Unread =>
			match seen.by_module.get(module_named) {
				Ok(names_held) => Ok(names_held)
				Err(_) => Err(Unknown)
			}
	}
	{ names, seen: after }
}

## Every name a file's tree holds: what it declares, binds, calls, each
## record field and method it names, and each symbol a platform's
## `provides` gives the host, with and without an effect's `!`.
names_in_tree : List(Tree.Node) -> Set(Str)
names_in_tree = |nodes| {
	var $names = Set.empty()
	for node in nodes {
		for attribute in node.attributes {
			if ["raw", "name", "field", "method", "symbol"].contains(attribute.name) {
				value = attribute.value.drop_prefix(".")
				if value != "" {
					$names = $names.insert(value)
					if value.ends_with("!") {
						$names = $names.insert(value.drop_suffix("!"))
					}
				}
			}
		}
	}
	$names
}

## The modules a file imports, by the name its code calls them (`Route`,
## and `Stdout` for `pf.Stdout`).
imports_in_tree : List(Tree.Node) -> List(Str)
imports_in_tree = |nodes|
	nodes.keep_if(|node| node.kind == "s-import").map(|node| Tree.last_part(Tree.attribute(node, "raw")))

## A file's extensions, so `Screen.roc` is a file's name, not a member's.
file_extensions : List(Str)
file_extensions = ["roc", "md", "txt", "zig", "ts", "js", "mjs", "json", "shale", "html", "tsv"]

## Each backticked name in the comment lines of a file (`#` or `##` first),
## as the header says which are judged.
doc_names_in : List(Str), Set(Str) -> List(DocName)
doc_names_in = |source_lines, sources| {
	var $out = []
	$number : U64
	var $number = 0
	for source_line in source_lines {
		$number = $number + 1
		line = Text.trim(source_line)
		if line.starts_with("#") {
			for span in backticked(line) {
				match doc_name_parts(span.text) {
					Ok(parts) => {
						before_on_line = Text.trim(without_hashes(Text.bytes_of(line, { start: 0, count: span.at })))
						before = if before_on_line == "" and $number > 1 {
							Text.trim(Text.line_at(source_lines, $number - 1))
						} else {
							before_on_line
						}
						is_others = is_possessive("${before} ") or cites_source(sentence_before(source_lines, $number, span.at), sources)
						is_file = parts.module_part != "" and file_extensions.contains(parts.name)
						if (parts.module_part != "" or parts.name.contains("_")) and !(is_file) {
							$out = $out.append({ line: $number, module_part: parts.module_part, name: parts.name, is_others })
						}
					}
					Err(NoName) => {}
				}
			}
		}
	}
	$out
}

## Each run between two backticks with something between them, left to
## right: `at` is how many bytes stand before its opening backtick.
backticked : Str -> List({ at : U64, text : Str })
backticked = |line| {
	bytes = line.to_utf8()
	var $spans = []
	$at : U64
	var $at = 0
	while $at < bytes.len() {
		if bytes.get($at) == Ok('`') {
			rest = bytes.drop_first($at + 1)
			match rest.find_first_index(|byte| byte == '`') {
				Ok(0) => {
					$at = $at + 1
				}
				Ok(length) => {
					$spans = $spans.append({ at: $at, text: Str.from_utf8_lossy(rest.take_first(length)) })
					$at = $at + length + 2
				}
				Err(_) => {
					$at = bytes.len()
				}
			}
		} else {
			$at = $at + 1
		}
	}
	$spans
}

## A backticked text's module and name when it is a doc name:
## `two_opt`, `read_line!`, `Route.two_opt(stops)`
## (the Shale original's DOC_NAME).
doc_name_parts : Str -> Try({ module_part : Str, name : Str }, [NoName])
doc_name_parts = |text| {
	bytes = text.to_utf8()
	match bytes {
		[first, .. as rest] if Text.is_capital(first) => {
			module_length = 1 + (rest.find_first_index(|byte| !(is_letter_or_digit(byte))) ?? rest.len())
			if bytes.get(module_length) != Ok('.') {
				return Err(NoName)
			}
			name = name_of(bytes.drop_first(module_length + 1))?
			Ok({ module_part: Str.from_utf8_lossy(bytes.take_first(module_length)), name })
		}
		_ => Ok({ module_part: "", name: name_of(bytes)? })
	}
}

## The name a doc name's bytes start with, `[a-z][a-z0-9]*(_[a-z0-9]+)*!?`,
## when nothing or a call's parentheses follow it.
name_of : List(U8) -> Try(Str, [NoName])
name_of = |bytes| {
	is_word_byte = |byte| Text.is_small_letter(byte) or Text.is_digit(byte)
	match bytes.first() {
		Ok(first) if Text.is_small_letter(first) => {}
		_ => return Err(NoName)
	}
	$length : U64
	var $length = 1
	while $length < bytes.len() and is_word_byte(bytes.get($length) ?? 0) {
		$length = $length + 1
	}
	while bytes.get($length) == Ok('_') and is_word_byte(bytes.get($length + 1) ?? 0) {
		$length = $length + 2
		while $length < bytes.len() and is_word_byte(bytes.get($length) ?? 0) {
			$length = $length + 1
		}
	}
	if bytes.get($length) == Ok('!') {
		$length = $length + 1
	}
	after = bytes.drop_first($length)
	is_call = after.len() >= 2 and after.first() == Ok('(') and after.last() == Ok(')')
	if after.is_empty() or is_call {
		Ok(Str.from_utf8_lossy(bytes.take_first($length)))
	} else {
		Err(NoName)
	}
}

is_letter_or_digit : U8 -> Bool
is_letter_or_digit = |byte| Text.is_capital(byte) or Text.is_small_letter(byte) or Text.is_digit(byte)

## A text without the `#`s that start it.
without_hashes : Str -> Str
without_hashes = |text| {
	bytes = text.to_utf8()
	start = bytes.find_first_index(|byte| byte != '#') ?? bytes.len()
	Str.from_utf8_lossy(bytes.drop_first(start))
}

## Whether a text, a space after it, ends in a possessive: `'s` or `s'`,
## with `own` after it or not (`Pipeline's`, `the rules' own`).
is_possessive : Str -> Bool
is_possessive = |text| ["'s ", "s' ", "'s own ", "s' own "].any(|ending| text.ends_with(ending))

is_space : U8 -> Bool
is_space = |byte| byte == ' ' or byte == '\t' or byte == '\n' or byte == '\r' or byte == 12 or byte == 11

## The places of a text's sentence ends: a `.`, `?` or `!` with a space or
## the text's end after it.
sentence_ends : Str -> List(U64)
sentence_ends = |text| {
	bytes = text.to_utf8()
	var $ends = []
	$at : U64
	var $at = 0
	for byte in bytes {
		is_mark = byte == '.' or byte == '?' or byte == '!'
		is_last = $at + 1 == bytes.len()
		if is_mark and (is_last or is_space(bytes.get($at + 1) ?? 0)) {
			$ends = $ends.append($at)
		}
		$at = $at + 1
	}
	$ends
}

## The text of the sentence before byte `at` of comment line `number`:
## back across the comment lines above, their `#`s gone, until a `.`, `?`
## or `!` that ends a sentence, a blank comment line, or the comment's
## start.
sentence_before : List(Str), U64, U64 -> Str
sentence_before = |source_lines, number, at| {
	on_line = Text.bytes_of(Text.trim(Text.line_at(source_lines, number)), { start: 0, count: at })
	var $text = Text.trim(without_hashes(on_line))
	var $up = number - 1
	var $is_blank_above = Bool.False
	while !($is_blank_above) and sentence_ends($text).is_empty() and $up >= 1 and Text.trim(Text.line_at(source_lines, $up)).starts_with("#") {
		above = Text.trim(without_hashes(Text.trim(Text.line_at(source_lines, $up))))
		if above == "" {
			$is_blank_above = Bool.True
		} else {
			$text = "${above} ${$text}"
			$up = $up - 1
		}
	}
	match sentence_ends($text).last() {
		Ok(last) => Text.bytes_of($text, { start: last + 1, count: $text.count_utf8_bytes() })
		Err(_) => $text
	}
}

## Whether a sentence names a source a citation names, as a whole word or a
## possessive (`Rust`, `Rust's`, `Zig,`).
cites_source : Str, Set(Str) -> Bool
cites_source = |sentence, sources|
	Text.words(sentence).any(
		|word| {
			unpossessed = if word.ends_with("'s") {
				word.drop_suffix("'s")
			} else if word.ends_with("s'") {
				word.drop_suffix("s'")
			} else {
				word
			}
			bare = Str.from_utf8_lossy(unpossessed.to_utf8().drop_if(|byte| byte == ',' or byte == ';' or byte == ':' or byte == '(' or byte == ')' or byte == '"'))
			sources.contains(bare)
		},
	)
