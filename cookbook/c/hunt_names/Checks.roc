## hunt_names' checks of one file, every one but `abbreviation`, which needs
## the whole tree's names: a type's, a function's and a constant's case, a
## bound's words and their order, a vague function or type, `do`, and a
## vague local or parameter. The file comes split by the C recipes'
## splitter (cookbook/c/items/); each check is cookbook/c/hunt_names.shale's,
## the Shale original's, and its words are the original's.
import items.Item
import items.Mask
import items.Split
import Declared
import Names

Checks := [].{

	## A finding: its file and line, its check and why it is one.
	Finding : { path : Str, line : U64, check : Str, why : Str }

	## A top-level name the file declares, at its item's first line, with its
	## words, for `abbreviation`.
	Named : { path : Str, line : U64, name : Str, words : List(Str) }

	## The word tables the checks read: cookbook/words/vague.txt's,
	## containers.txt's (and `data`, `info`), and c_abbreviations.txt's, an
	## abbreviation to its word.
	Tables : { vague : Set(Str), containers : Set(Str), abbreviations : Dict(Str, Str) }

	## The checks, in the order the counts give them.
	check_names : List(Str)
	check_names = ["type_case", "function_case", "constant_case", "zig_bound", "bound_order", "vague", "do_word", "vague_local", "container_type", "abbreviation"]

	## A finding's line: `path:line: check: why`.
	finding_line : Checks.Finding -> Str
	finding_line = |finding| "${finding.path}:${finding.line.to_str()}: ${finding.check}: ${finding.why}"

	## The file's findings by every check but `abbreviation`, and its
	## top-level names, in the order of its items.
	of_file : Str, List(List(U8)), List(Item), Checks.Tables -> { found : List(Checks.Finding), named : List(Checks.Named) }
	of_file = |path, lines, items, tables| {
		var $found = []
		var $names = []
		for item in items {
			opening = lines.get(item.first - 1) ?? []
			type = type_checks(item, opening, tables)
			constants = constant_checks(item)
			function = function_checks(lines, item, tables)
			$found = $found.concat(type.found).concat(constants.found).concat(function.found)
			named_here = type.names.concat(constants.names).concat(other_names(item)).concat(function.names)
			$names = $names.concat(named_here.map(|name| { name, line: item.first }))
		}
		finding = |each| { path, line: each.line, check: each.check, why: each.why }
		named = |each| { path, line: each.line, name: each.name, words: Names.words_of(each.name) }
		{ found: $found.map(finding), named: $names.map(named) }
	}
}

## A finding before its file is known: its line, its check and why.
Found : { line : U64, check : Str, why : Str }

## What a check makes of an item: its findings, and the names it declares.
Checked : { found : List(Found), names : List(Str) }

## The names a local or a parameter says nothing with.
vague_locals : List(Str)
vague_locals = ["data", "info", "tmp", "temp", "stuff", "thing", "things", "misc", "util", "helper", "obj", "handle"]

## A function's lone word that is an entry point's, never vague alone.
entry_words : List(Str)
entry_words = ["run", "handle"]

## A unit a bound's name may end with, after its `MAX` or `MIN`.
units : List(Str)
units = ["MS", "NS", "US", "S", "BYTES"]

## Whether there are words and every one of them is in the table.
is_only : List(Str), Set(Str) -> Bool
is_only = |words, table| !(words.is_empty()) and words.all(|word| table.contains(word))

## A type's checks: a struct, union, typedef or enum, but no anonymous enum
## (its first constant its name, and no `typedef` opening it), which is
## constants, not a type.
type_checks : Item, List(U8), Checks.Tables -> Checked
type_checks = |item, opening, tables| {
	is_type = item.kind == Struct or item.kind == Union or item.kind == Typedef or item.kind == Enum
	is_anonymous = item.kind == Enum and item.enumerators.first() == Ok(item.name) and !(opening.starts_with("typedef".to_utf8()))
	if is_type and !(is_anonymous) {
		name = item.name
		case = if Names.is_pascal_case(name) {
			[]
		} else {
			[{ line: item.first, check: "type_case", why: "the type `${name}` is not PascalCase" }]
		}
		container = if is_only(Names.words_of(name), tables.containers) {
			[{ line: item.first, check: "container_type", why: "the type `${name}` is only container words: name it for what it holds" }]
		} else {
			[]
		}
		{ found: case.concat(container), names: [name] }
	} else {
		{ found: [], names: [] }
	}
}

## A constant's checks: each of an enum's constants, and a macro named in
## UPPER_SNAKE: its case, Zig's bound words, and a bound not last.
constant_checks : Item -> Checked
constant_checks = |item| {
	macro = if item.kind == Macro and Names.is_upper_snake(item.name) {
		[item.name]
	} else {
		[]
	}
	constants = item.enumerators.concat(macro)
	cased =
		item.enumerators
			.keep_if(|constant| !(Names.is_upper_snake(constant)))
			.map(|constant| { line: item.first, check: "constant_case", why: "the constant `${constant}` is not UPPER_SNAKE" })
	bounds = constants.fold([], |found, constant| found.concat(bound_checks(item.first, constant)))
	{ found: cased.concat(bounds), names: constants }
}

## A constant's bound checks: `MOST` or `LEAST` among its words, and its
## first `MAX` or `MIN` before its last word, unless a unit alone follows it.
bound_checks : U64, Str -> List(Found)
bound_checks = |line, constant| {
	words = constant.split_on("_")
	zig = if words.contains("MOST") or words.contains("LEAST") {
		[{ line, check: "zig_bound", why: "`${constant}` is a bound in Zig's words: C's are `_MAX` and `_MIN`" }]
	} else {
		[]
	}
	count = words.len()
	last_word = words.last() ?? ""
	is_misplaced = |(at, word)|
		(word == "MAX" or word == "MIN") and at + 1 < count and !(at + 2 == count and units.contains(last_word))
	order =
		match words.map_with_index(|word, at| (at, word)).find_first(is_misplaced) {
			Ok((at, word)) => {
				moved = words.drop_at(at).append(word)
				why = "`${constant}` has `${word}` before its last word: a bound ends with it, as `${Str.join_with(moved, "_")}` would"
				[{ line, check: "bound_order", why }]
			}
			Err(_) => []
		}
	zig.concat(order)
}

## The names of a macro not in UPPER_SNAKE and of named data, which no
## check reads but `abbreviation`.
other_names : Item -> List(Str)
other_names = |item|
	if item.kind == Macro and !(Names.is_upper_snake(item.name)) {
		[item.name]
	} else if item.kind == Data and item.name != "" {
		[item.name]
	} else {
		[]
	}

## A function's checks: its case (`main` aside), vague words alone, `do`,
## and its vague parameters and locals.
function_checks : List(List(U8)), Item, Checks.Tables -> Checked
function_checks = |lines, item, tables| {
	if item.kind != Function {
		return { found: [], names: [] }
	}
	name = item.name
	at = item.first
	case = if name == "main" or Names.is_snake_case(name) {
		[]
	} else {
		[{ line: at, check: "function_case", why: "the function `${name}` is not snake_case" }]
	}
	words = Names.words_of(name)
	is_entry = words.len() == 1 and words.all(|word| entry_words.contains(word))
	vague = if is_only(words, tables.vague) and !(is_entry) {
		[{ line: at, check: "vague", why: "`${name}` is only vague words: what does it act on, and how?" }]
	} else {
		[]
	}
	do_word = if name.split_on("_").contains("do") {
		[{ line: at, check: "do_word", why: "`${name}` has `do` in it, which says only that it acts: name the act" }]
	} else {
		[]
	}
	parameters =
		Declared.parameters(head_of(lines, item))
			.keep_if(|parameter| vague_locals.contains(parameter))
			.map(|parameter| { line: at, check: "vague_local", why: "`${name}`'s parameter `${parameter}` says nothing of what it holds" })
	{ found: case.concat(vague).concat(do_word).concat(parameters).concat(vague_body_locals(lines, item)), names: [name] }
}

## A function's head, masked, each run of `\s` one space; a head that shares
## its line with the body ends before the body's `{`.
head_of : List(List(U8)), Item -> List(U8)
head_of = |lines, item| {
	head = spaces_made_one(Split.masked_text(lines, { first: item.sig_first, last: item.sig_last }))
	if item.body_first == item.sig_last {
		match Mask.byte_from(head, '{', 0) {
			Ok(brace) => head.take_first(brace)
			Err(_) => head
		}
	} else {
		head
	}
}

## The vague locals a function's body declares, a finding at each one's
## line: a body below its head, each line masked.
vague_body_locals : List(List(U8)), Item -> List(Found)
vague_body_locals = |lines, item| {
	if item.body_first == 0 or item.body_first == item.sig_last {
		return []
	}
	var $found = []
	var $at = item.body_first
	while $at <= item.body_last {
		match Declared.local(Mask.masked(lines.get($at - 1) ?? [])) {
			Ok(local) if vague_locals.contains(local) => {
				why = "`${item.name}`'s local `${local}` says nothing of what it holds"
				$found = $found.append({ line: $at, check: "vague_local", why })
			}
			_ => {}
		}
		$at = $at + 1
	}
	$found
}

## The text with each run of `\s` made one space.
spaces_made_one : List(U8) -> List(U8)
spaces_made_one = |text|
	text.fold(
		[],
		|out, byte|
			if !(Mask.is_space(byte)) {
				out.append(byte)
			} else if out.last() == Ok(' ') {
				# A space is `\s`, so one that ends `out` is a run's already.
				out
			} else {
				out.append(' ')
			},
	)
