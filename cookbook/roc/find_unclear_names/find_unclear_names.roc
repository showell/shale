## find_unclear_names, in Roc on Shale's platform (roc_platform/README.md):
## names and top comments that say less than they should. Its launcher,
## cookbook/roc/find_unclear_names.shale, is how it is run, and its header
## says the rules, `pure_mutator`, `abbreviation` and `module_doc`.
## check_goldens.shale, beside this, holds its output on the plants to its
## goldens, line for line.
##
##   cd <repo> && shale cookbook:roc/find_unclear_names.shale glob=app/**/*.roc
##   shale cookbook:roc/find_unclear_names.shale self_test=1
##
## The arguments: `glob` (`**/*.roc`), `abbreviations`, `words`, `accepted`,
## `terms`, `house` (`house=0` leaves out the house style, `abbreviation`) and
## `self_test` (`self_test=1` runs the plants, each through the launcher).
## budget: steps=800000000 arena_bytes=2000000000 output_bytes=4000000 roc_cpu_ms=600000 programs=100
app [main!] { pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale

## A node of Roc's tree, as `Shale.roc_tree!` gives it: `parent` and `kids`
## are places in the tree counted from 1, the root's parent 0.
Node : { kind : Str, depth : U64, parent : U64, kids : List(U64), line : U64, col : U64, end_line : U64, end_col : U64, attrs : List({ name : Str, value : Str }) }

Finding : { path : Str, line : U64, check : Str, what : Str }

## What a file declares at its top level or as a module type's member: a
## type, a value (an annotation, or a definition with none above it) or a
## record field of a type or an annotation.
Declared : {
	line : U64,
	kind : [Type, Value, Field],
	name : Str,
	owner : Str,
	annotated : Bool,
	fn : Bool,
	result : Str,
	first : Str,
	is_nominal : Bool,
	underlying : Str,
}

Lists : { abbreviations : Set(Str), words : Set(Str), accepted : Set(Str) }

checks : List(Str)
checks = ["pure_mutator", "abbreviation", "module_doc"]

## The launcher, which the self-test runs each plant through.
launcher : Str
launcher = "cookbook:roc/find_unclear_names.shale"

main! : {} => {}
main! = |{}| {
	if Shale.arg!("self_test", "") == "1" {
		self_test!({})
	} else {
		verdict = run_check!({})
		if !(List.is_empty(verdict.found)) {
			Shale.fail!(verdict.summary)
		}
	}
}

# ── Text, as Shale's builtins cut it ─────────────────────────────────────

## A text's lines, as Shale's `lines` splits it: a last newline ends a line.
lines : Str -> List(Str)
lines = |text| {
	parts = Str.split_on(text, "\n")
	if Str.ends_with(text, "\n") {
		List.drop_last(parts, 1)
	} else {
		parts
	}
}

## Line `k` of `src`, counted from 1, or `""` past its end.
line_at : List(Str), U64 -> Str
line_at = |src, k|
	if k == 0 {
		""
	} else {
		List.get(src, k - 1) ?? ""
	}

## The text before the first `sep`, or all of it (Shale's `split(s, sep)[1]`).
before_first : Str, Str -> Str
before_first = |s, sep|
	match Str.split_first(s, sep) {
		Ok(parts) => parts.before
		Err(_) => s
	}

## The text after the last `sep`, or all of it.
after_last : Str, Str -> Str
after_last = |s, sep|
	match Str.split_last(s, sep) {
		Ok(parts) => parts.after
		Err(_) => s
	}

## Shale's `between(s, a, b)`: after the first `a`, before the next `b`.
between : Str, Str, Str -> Try(Str, [NotFound])
between = |s, a, b| {
	first = Str.split_first(s, a)?
	second = Str.split_first(first.after, b)?
	Ok(second.before)
}

## Shale's `dirname`: the path up to its last `/`, or `""`.
dirname : Str -> Str
dirname = |path|
	match Str.split_last(path, "/") {
		Ok(parts) => parts.before
		Err(_) => ""
	}

basename : Str -> Str
basename = |path| after_last(path, "/")

byte_count : Str -> U64
byte_count = |s| List.len(Str.to_utf8(s))

## The bytes of `s` from place `from` (counted from 0), `n` of them, as text.
bytes_of : Str, U64, U64 -> Str
bytes_of = |s, from, n| Str.from_utf8_lossy(List.sublist(Str.to_utf8(s), { start: from, len: n }))

is_upper : U8 -> Bool
is_upper = |b| b >= 65 and b <= 90

is_lower : U8 -> Bool
is_lower = |b| b >= 97 and b <= 122

is_digit : U8 -> Bool
is_digit = |b| b >= 48 and b <= 57

lower_bytes : List(U8) -> Str
lower_bytes = |bytes| Str.from_utf8_lossy(bytes.map(|b| if is_upper(b) { b + 32 } else { b }))

## Bytewise order, as Shale's `sort` puts strings.
byte_order : Str, Str -> [Before, Same, After]
byte_order = |a, b| {
	x = Str.to_utf8(a)
	y = Str.to_utf8(b)
	var $k = 0
	var $answer = Same
	var $going = Bool.True
	while $going {
		match (List.get(x, $k), List.get(y, $k)) {
			(Ok(p), Ok(q)) =>
				if p < q {
					$answer = Before
					$going = Bool.False
				} else if p > q {
					$answer = After
					$going = Bool.False
				} else {
					$k = $k + 1
				}
			(Err(_), Ok(_)) => {
				$answer = Before
				$going = Bool.False
			}
			(Ok(_), Err(_)) => {
				$answer = After
				$going = Bool.False
			}
			(Err(_), Err(_)) => {
				$going = Bool.False
			}
		}
	}
	$answer
}

# ── The tree ─────────────────────────────────────────────────────────────

node_at : List(Node), U64 -> Node
node_at = |tree, at|
	match List.get(tree, at - 1) {
		Ok(n) => n
		Err(_) => crash "find_unclear_names: the tree has no node ${U64.to_str(at)}"
	}

## A node's attribute, `""` where the tree gives it none.
attribute : Node, Str -> Str
attribute = |n, name|
	match List.find_first(n.attrs, |a| a.name == name) {
		Ok(a) => a.value
		Err(_) => ""
	}

first_kid : Node -> U64
first_kid = |n|
	match List.first(n.kids) {
		Ok(k) => k
		Err(_) => crash "find_unclear_names: a ${n.kind} with no kids"
	}

last_kid : Node -> U64
last_kid = |n|
	match List.last(n.kids) {
		Ok(k) => k
		Err(_) => crash "find_unclear_names: a ${n.kind} with no kids"
	}

kid_kind : List(Node), Node, U64 -> Str
kid_kind = |tree, n, k|
	match List.get(n.kids, k) {
		Ok(at) => node_at(tree, at).kind
		Err(_) => ""
	}

## A dotted type's last part.
last_part : Str -> Str
last_part = |type_name| after_last(type_name, ".")

## The type at `at`, as Roc writes it: `List(U8)`, `{ id : U8 }`, `[A, B]`,
## `(U8 -> Bool)`; both arrows come out as `->`.
render_type : List(Node), U64 -> Str
render_type = |tree, at| {
	n = node_at(tree, at)
	kind = n.kind
	name = attribute(n, "name")
	parts = |kids| Str.join_with(kids.map(|c| render_type(tree, c)), ", ")
	if kind == "ty-apply" {
		"${render_type(tree, first_kid(n))}(${parts(List.drop_first(n.kids, 1))})"
	} else if kind == "ty-fn" {
		"(${parts(List.drop_last(n.kids, 1))} -> ${render_type(tree, last_kid(n))})"
	} else if kind == "ty-record" {
		"{ ${parts(n.kids)} }"
	} else if kind == "ty-tuple" {
		"(${parts(n.kids)})"
	} else if kind == "tags" {
		parts(n.kids)
	} else if kind == "ty-tag-union" {
		"[${render_type(tree, first_kid(n))}]"
	} else if kind == "anno-record-field" {
		"${name} : ${render_type(tree, first_kid(n))}"
	} else if name != "" {
		name
	} else if kind == "_" {
		"_"
	} else {
		raw = attribute(n, "raw")
		if raw != "" { raw } else { "(${kind})" }
	}
}

## A name's words, lowercased: a type's split at its capitals (`NodeInfo`
## gives `node`, `info`), a field's at its underscores.
name_words : Str -> List(Str)
name_words = |name| {
	var $out = []
	var $current = []
	for b in Str.to_utf8(name) {
		if b == 95 {
			if !(List.is_empty($current)) {
				$out = $out.append(lower_bytes($current))
			}
			$current = []
		} else if is_upper(b) and !(List.is_empty($current)) {
			$out = $out.append(lower_bytes($current))
			$current = [b]
		} else {
			$current = $current.append(b)
		}
	}
	if List.is_empty($current) {
		$out
	} else {
		$out.append(lower_bytes($current))
	}
}

module_name : Str -> Str
module_name = |path| Str.drop_suffix(basename(path), ".roc")

is_app : List(Node) -> Bool
is_app = |tree| List.any(tree, |n| n.kind == "app")

## The kinds a definition may sit in and still be top-level or a module
## type's member.
is_outer_kind : Str -> Bool
is_outer_kind = |kind| kind == "file" or kind == "statements" or kind == "s-type-decl" or kind == "associated"

## The name of the type declaration around node `at`, or "" at the top level.
owner_of : List(Node), U64 -> Str
owner_of = |tree, at| {
	var $k = node_at(tree, at).parent
	while $k != 0 {
		n = node_at(tree, $k)
		if n.kind == "s-type-decl" {
			return attribute(node_at(tree, first_kid(n)), "name")
		}
		$k = n.parent
	}
	""
}

## The line of the declaration a record field belongs to, as
## `declaration_lines` holds it, or 0 when an expression, a pattern or a
## definition lies between them (the field is then a local's).
field_declaration_line : List(Node), List(U64), U64 -> U64
field_declaration_line = |tree, declaration_lines, at| {
	var $k = node_at(tree, at).parent
	while $k != 0 {
		n = node_at(tree, $k)
		if Str.starts_with(n.kind, "e-") or Str.starts_with(n.kind, "p-") or n.kind == "s-decl" {
			return 0
		}
		if n.kind == "s-type-anno" or n.kind == "s-type-decl" {
			return List.get(declaration_lines, $k - 1) ?? 0
		}
		$k = n.parent
	}
	0
}

plain : U64, [Type, Value, Field], Str, Str -> Declared
plain = |line, kind, name, owner| { line, kind, name, owner, annotated: Bool.False, fn: Bool.False, result: "", first: "", is_nominal: Bool.False, underlying: "" }

## The annotation at `i`, unless it is only a `where` clause.
annotated_value : List(Node), U64 -> List(Declared)
annotated_value = |tree, i| {
	n = node_at(tree, i)
	match List.first(n.kids) {
		Err(_) => []
		Ok(type_at) => {
			type_node = node_at(tree, type_at)
			if type_node.kind == "where" {
				return []
			}
			base = { ..plain(n.line, Value, attribute(n, "name"), owner_of(tree, i)), annotated: Bool.True, result: render_type(tree, type_at) }
			if type_node.kind == "ty-fn" {
				first = if List.len(type_node.kids) > 1 { render_type(tree, first_kid(type_node)) } else { "" }
				[{ ..base, fn: Bool.True, result: render_type(tree, last_kid(type_node)), first }]
			} else {
				[base]
			}
		}
	}
}

## The type declared at `i`: `is_nominal` says it is declared with `:=` or
## `::`, and `underlying` is the type it is built on.
declared_type : List(Node), List(Str), U64 -> List(Declared)
declared_type = |tree, src, i| {
	n = node_at(tree, i)
	name_node = node_at(tree, first_kid(n))
	name = attribute(name_node, "name")
	if name == "" {
		return []
	}
	base = plain(n.line, Type, name, owner_of(tree, i))
	match List.get(n.kids, 1) {
		Ok(built_at) =>
			if node_at(tree, built_at).kind == "associated" {
				[base]
			} else {
				between_text = bytes_of(line_at(src, name_node.end_line), name_node.end_col - 1, 4)
				is_nominal = Str.contains(between_text, ":=") or Str.contains(between_text, "::")
				[{ ..base, is_nominal, underlying: render_type(tree, built_at) }]
			}
		Err(_) => [base]
	}
}

## Everything the file declares at its top level or as a module type's
## member, in file order (cookbook/roc/declared.shale's `declarations_of`).
declarations_of : List(Node), List(Str) -> List(Declared)
declarations_of = |tree, src| {
	var $outer = []
	var $declaration_lines = []
	var $annotated = Set.empty()
	var $out = []
	var $i = 0
	for n in tree {
		$i = $i + 1
		i = $i
		parent_outer = n.parent != 0 and (List.get($outer, n.parent - 1) ?? Bool.False) and is_outer_kind(node_at(tree, n.parent).kind)
		is_outer = n.parent == 0 or parent_outer
		$outer = $outer.append(is_outer)
		kind = n.kind
		$declaration_lines = $declaration_lines.append(if is_outer and (kind == "s-type-anno" or kind == "s-type-decl") { n.line } else { 0 })
		if is_outer and kind == "s-type-decl" {
			$out = List.concat($out, declared_type(tree, src, i))
		}
		if kind == "anno-record-field" and field_declaration_line(tree, $declaration_lines, i) != 0 {
			$out = $out.append(plain(n.line, Field, attribute(n, "name"), owner_of(tree, i)))
		}
		if is_outer and kind == "s-type-anno" {
			for d in annotated_value(tree, i) {
				$out = $out.append(d)
				$annotated = Set.insert($annotated, "${d.owner}.${d.name}")
			}
		}
		if is_outer and kind == "s-decl" and kid_kind(tree, n, 0) == "p-ident" {
			name = attribute(node_at(tree, first_kid(n)), "raw")
			owner = owner_of(tree, i)
			if !(Set.contains($annotated, "${owner}.${name}")) {
				$out = $out.append({ ..plain(n.line, Value, name, owner), fn: kid_kind(tree, n, 1) == "e-lambda" })
			}
		}
	}
	$out
}

# ── pure_mutator ─────────────────────────────────────────────────────────

## The value of a `Try(value, errors)`, or `result` when it is no `Try`.
value_of_try : Str -> Str
value_of_try = |result| {
	if !(Str.starts_with(result, "Try(")) {
		return result
	}
	bytes = Str.to_utf8(result)
	var $depth = 0
	var $k = 4
	while $k < List.len(bytes) {
		b = List.get(bytes, $k) ?? 0
		if b == 40 or b == 123 or b == 91 {
			$depth = $depth + 1
		} else if b == 41 or b == 125 or b == 93 {
			if $depth == 0 {
				return result
			}
			$depth = $depth - 1
		} else if b == 44 and $depth == 0 {
			return Str.trim(bytes_of(result, 4, $k - 4))
		}
		$k = $k + 1
	}
	result
}

record_holds : Str, Str -> Bool
record_holds = |record, type_name|
	Str.starts_with(record, "{") and (Str.contains(record, " : ${type_name},") or Str.contains(record, " : ${type_name} }"))

## Whether a function's result is what its first argument was added to.
gives_first_back : Declared, Dict(Str, Str) -> Bool
gives_first_back = |d, aliases| {
	value = value_of_try(d.result)
	if value == d.first or record_holds(d.first, value) {
		Bool.True
	} else {
		record_holds(Dict.get(aliases, last_part(value)) ?? value, d.first)
	}
}

opens_with_term : Str, Set(Str) -> Bool
opens_with_term = |name, terms| Set.to_list(terms).any(|term| name == term or Str.starts_with(name, "${term}_"))

is_mutation_word : Str -> Bool
is_mutation_word = |w| w == "push" or w == "fill" or w == "set" or w == "append" or w == "add"

judge_mutator : Str, Declared, Dict(Str, Str), Set(Str) -> List(Finding)
judge_mutator = |path, d, aliases, terms| {
	if !(d.fn) or !(d.annotated) or Str.ends_with(d.name, "!") or opens_with_term(d.name, terms) {
		return []
	}
	first_word = before_first(d.name, "_")
	is_mutation = is_mutation_word(first_word) and !(gives_first_back(d, aliases))
	if first_word == "get" or is_mutation {
		[{ path, line: d.line, check: "pure_mutator", what: "`${d.name}` is pure and gives a new `${d.result}`: name it for what it gives" }]
	} else {
		[]
	}
}

# ── abbreviation ─────────────────────────────────────────────────────────

## Whether a short word stands as it is: an English word, an accepted term,
## or a letter and an axis (`dx`, `vy`).
is_plain_short_word : Str, Lists -> Bool
is_plain_short_word = |w, lists| {
	if Set.contains(lists.words, w) or Set.contains(lists.accepted, w) {
		Bool.True
	} else {
		bytes = Str.to_utf8(w)
		second = List.get(bytes, 1) ?? 0
		List.len(bytes) == 2 and (second == 120 or second == 121 or second == 122)
	}
}

## Whether a word of two to four letters is short for another.
is_short_abbreviation : Str, Lists -> Bool
is_short_abbreviation = |w, lists| {
	bytes = Str.to_utf8(w)
	n = List.len(bytes)
	if n < 2 or n > 4 or !(List.all(bytes, is_lower)) {
		Bool.False
	} else {
		!(is_plain_short_word(w, lists))
	}
}

## The stems a word may be the plural of (`shapes`: `shape`, `shap`; `tries`:
## `trie`, `tri`, `try`), none for a word ending as no plural does.
plural_stems : Str -> List(Str)
plural_stems = |w| {
	n = byte_count(w)
	no_plural = ["ss", "us", "is", "as", "os"].any(|e| Str.ends_with(w, e))
	if n < 3 or !(Str.ends_with(w, "s")) or no_plural {
		return []
	}
	one = [bytes_of(w, 0, n - 1)]
	two = if Str.ends_with(w, "es") and n > 3 { one.append(bytes_of(w, 0, n - 2)) } else { one }
	if Str.ends_with(w, "ies") and n > 4 {
		two.append("${bytes_of(w, 0, n - 3)}y")
	} else {
		two
	}
}

## Whether a stem is a whole word, so its plural is plain.
is_whole_stem : Str, Lists -> Bool
is_whole_stem = |stem, lists|
	if Set.contains(lists.accepted, stem) {
		Bool.True
	} else if byte_count(stem) <= 4 {
		is_plain_short_word(stem, lists)
	} else {
		!(Set.contains(lists.abbreviations, stem))
	}

## Whether a word of a name is an abbreviation (the launcher's header says
## the rule).
is_abbreviated : Str, Lists -> Bool
is_abbreviated = |word, lists| {
	letters = Str.to_utf8(word).keep_if(|b| is_lower(b) or is_digit(b))
	whole = Str.from_utf8_lossy(letters)
	if Set.contains(lists.accepted, whole) {
		return Bool.False
	}
	if Set.contains(lists.abbreviations, whole) {
		return Bool.True
	}
	var $end = List.len(letters)
	while $end > 0 and is_digit(List.get(letters, $end - 1) ?? 0) {
		$end = $end - 1
	}
	w = Str.from_utf8_lossy(List.take_first(letters, $end))
	if Set.contains(lists.accepted, w) {
		return Bool.False
	}
	if Set.contains(lists.abbreviations, w) {
		return Bool.True
	}
	if byte_count(w) <= 4 and is_plain_short_word(w, lists) {
		return Bool.False
	}
	stems = plural_stems(w)
	if stems.any(|stem| is_whole_stem(stem, lists)) {
		Bool.False
	} else if stems.any(|stem| Set.contains(lists.abbreviations, stem) or is_short_abbreviation(stem, lists)) {
		Bool.True
	} else {
		is_short_abbreviation(w, lists)
	}
}

## The tag a node names, `""` for none: a tag in a union, a pattern or an
## expression, a module's qualified tag by its last part.
tag_named : List(Node), U64 -> Str
tag_named = |tree, at| {
	n = node_at(tree, at)
	if n.kind == "p-tag" or n.kind == "e-tag" {
		return last_part(attribute(n, "raw"))
	}
	if n.kind != "ty" or n.parent == 0 {
		return ""
	}
	holder = node_at(tree, n.parent)
	if holder.kind == "tags" {
		attribute(n, "name")
	} else if holder.kind == "ty-apply" and List.first(holder.kids) == Ok(at) and node_at(tree, holder.parent).kind == "tags" {
		attribute(n, "name")
	} else {
		""
	}
}

## The names a file gives or says, with the line each appears on.
names_given : Str, List(Node), List(Declared) -> List({ name : Str, line : U64 })
names_given = |path, tree, declared| {
	var $out = if is_app(tree) { [] } else { [{ name: module_name(path), line: 1 }] }
	for d in declared {
		$out = $out.append({ name: d.name, line: d.line })
	}
	var $i = 0
	for n in tree {
		$i = $i + 1
		if n.kind == "p-ident" {
			$out = $out.append({ name: attribute(n, "raw"), line: n.line })
		}
		tag = tag_named(tree, $i)
		if tag != "" and tag != "Ok" and tag != "Err" {
			$out = $out.append({ name: tag, line: n.line })
		}
	}
	$out
}

judge_abbreviations : Str, List(Node), List(Declared), Lists -> List(Finding)
judge_abbreviations = |path, tree, declared, lists| {
	var $seen = Set.empty()
	var $report = []
	for given in names_given(path, tree, declared) {
		if !(Set.contains($seen, given.name)) {
			$seen = Set.insert($seen, given.name)
			found = name_words(given.name).keep_if(|w| is_abbreviated(w, lists))
			if !(List.is_empty(found)) {
				$report = $report.append({ path, line: given.line, check: "abbreviation", what: "`${given.name}` abbreviates: ${Str.join_with(found, ", ")}" })
			}
		}
	}
	$report
}

# ── module_doc ───────────────────────────────────────────────────────────

## The history word in the comment lines, or `""`. Each line's leading `#`s
## go before the lines are joined, so a phrase wrapped across two lines reads
## as one.
history_word! : List(Str) => Str
history_word! = |comment| {
	var $words = []
	for l in comment {
		$words = $words.append(Str.trim(Shale.re_replace!(l, "^#+", "")))
	}
	match Shale.re_find!(Str.with_ascii_lowercased(Str.join_with($words, " ")), "\\b(originally|used to|emitted|ported)\\b") {
		Found(m) => List.first(m.groups) ?? ""
		NoMatch => ""
	}
}

## The comment lines just above line `at`, trimmed, and the line they start
## on (`at` when there are none).
comment_above : List(Str), U64 -> { lines : List(Str), first : U64 }
comment_above = |src, at| {
	var $first = at
	while $first > 1 and Str.starts_with(Str.trim(line_at(src, $first - 1)), "#") {
		$first = $first - 1
	}
	{ lines: List.sublist(src, { start: $first - 1, len: at - $first }).map(Str.trim), first: $first }
}

## The comment lines that open a file, trimmed.
opening_comment : List(Str) -> List(Str)
opening_comment = |src| {
	var $out = []
	var $going = Bool.True
	for l in src {
		if $going and Str.starts_with(Str.trim(l), "#") {
			$out = $out.append(Str.trim(l))
		} else {
			$going = Bool.False
		}
	}
	$out
}

## A file's text with its comment lines left out.
code_of : Str -> Str
code_of = |text| Str.join_with(lines(text).keep_if(|l| !(Str.starts_with(Str.trim(l), "#"))), "\n")

## The names in the list between `[` and `]` of `text`.
bracketed_names : Str -> List(Str)
bracketed_names = |text|
	Str.split_on(between(text, "[", "]") ?? "", ",").map(Str.trim).keep_if(|m| m != "")

## What a folder's `main.roc` exposes, when it is a package's or a
## platform's (cookbook/roc/headers.shale's `exposed_by_folder`).
exposed_by_folder! : Str => { is_package : Bool, modules : List(Str) }
exposed_by_folder! = |dir| {
	main = if dir == "" or dir == "." { "main.roc" } else { "${dir}/main.roc" }
	none = { is_package: Bool.False, modules: [] }
	match Shale.read!(main) {
		Missing => none
		Found(whole) => {
			text = code_of(whole)
			word = match Shale.re_find!(text, "[A-Za-z_]+") {
				Found(m) => m.text
				NoMatch => ""
			}
			if word == "package" {
				{ is_package: Bool.True, modules: bracketed_names(text) }
			} else if word == "platform" {
				{ is_package: Bool.True, modules: bracketed_names("${between(text, "exposes", "]") ?? ""}]") }
			} else {
				none
			}
		}
	}
}

## Whether a file is a module its package keeps to itself.
is_private_module! : Str => Bool
is_private_module! = |path| {
	exposed = exposed_by_folder!(dirname(path))
	exposed.is_package and !(List.contains(exposed.modules, module_name(path))) and basename(path) != "main.roc"
}

judge_module_doc! : Str, List(Node), List(Declared), List(Str) => List(Finding)
judge_module_doc! = |path, tree, declared, src| {
	if is_app(tree) {
		return []
	}
	name = module_name(path)
	type_line = List.fold(declared, 0, |at, d| if d.kind == Type and d.name == name and d.owner == "" { d.line } else { at })
	if type_line == 0 {
		return []
	}
	top = opening_comment(src)
	above_type = Str.trim(line_at(src, type_line - 1))
	opens_with_doc = Str.starts_with(List.first(top) ?? "", "##")
	has_doc = opens_with_doc or (type_line > 1 and Str.starts_with(above_type, "##"))
	kept_to_itself = (!(List.is_empty(top)) or Str.starts_with(Str.trim(line_at(src, if type_line > 1 { type_line - 1 } else { 1 })), "#")) and is_private_module!(path)
	var $report = []
	if !(has_doc) and !(kept_to_itself) {
		$report = $report.append({ path, line: 1, check: "module_doc", what: "`${name}` has no `##` doc saying what it is for" })
	}
	history = history_word!(top)
	if history != "" {
		$report = $report.append({ path, line: 1, check: "module_doc", what: "`${name}`'s top comment says `${history}`: say what the module does, and leave history to git" })
	}
	doc = comment_above(src, type_line)
	doc_history = history_word!(doc.lines)
	if doc.first > 1 and doc_history != "" {
		$report = $report.append({ path, line: doc.first, check: "module_doc", what: "`${name}`'s doc says `${doc_history}`: say what the module does, and leave history to git" })
	}
	$report
}

# ── The word lists ───────────────────────────────────────────────────────

read_word_list! : Str => Str
read_word_list! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => {
			Shale.fail!("no word list ${path}")
			""
		}
	}

## A list of one word per line, any note after `--` cut off, as a set. A
## word listed twice, or a line of two words, stops the run.
word_set! : Str => Set(Str)
word_set! = |path| {
	var $set = Set.empty()
	for l in lines(read_word_list!(path)) {
		word = Str.trim(before_first(l, "--"))
		if Str.contains(word, " ") or Set.contains($set, word) {
			Shale.fail!("${path}: `${word}` is no single new word")
		}
		if word != "" {
			$set = Set.insert($set, word)
		}
	}
	$set
}

## The accepted terms file: a term a line, each with its reason after `--`.
## A line without a reason stops the run.
accepted_terms! : Str => Set(Str)
accepted_terms! = |path| {
	for l in lines(read_word_list!(path)) {
		parts = Str.split_on(l, "--")
		term = Str.trim(List.first(parts) ?? "")
		reason = Str.trim(List.get(parts, 1) ?? "")
		if term != "" and reason == "" {
			Shale.fail!("${path}: `${term}` has no reason after `--`")
		}
	}
	word_set!(path)
}

# ── The run ──────────────────────────────────────────────────────────────

## Whether `house=0` leaves the house style out (cookbook/roc/house.shale).
house_is_off! : {} => Bool
house_is_off! = |{}| {
	house = Shale.arg!("house", "1")
	if house != "0" and house != "1" {
		Shale.fail!("house must be 1 (the default) or 0, not ${house}")
	}
	house == "0"
}

finding_order : Finding, Finding -> [Before, Same, After]
finding_order = |a, b|
	match byte_order(a.path, b.path) {
		Same =>
			if a.line < b.line {
				Before
			} else if a.line > b.line {
				After
			} else {
				byte_order(a.what, b.what)
			}
		other => other
	}

## The summary and the finding lines, sorted (cookbook/roc/gate_recipe.shale's
## `verdict_of`).
verdict_of : List(Finding), U64 -> { summary : Str, found : List(Str) }
verdict_of = |report, file_count| {
	sorted = List.sort_with(report, finding_order)
	counts = checks.map(|c| "${c} ${U64.to_str(List.count_if(sorted, |r| r.check == c))}")
	found = sorted.map(|r| "${r.path}:${U64.to_str(r.line)}: ${r.check}: ${r.what}")
	{ summary: "find_unclear_names (Roc): ${U64.to_str(file_count)} files; ${Str.join_with(counts, ", ")}", found }
}

run_check! : {} => { summary : Str, found : List(Str) }
run_check! = |{}| {
	abbreviations = word_set!(Shale.arg!("abbreviations", "cookbook:words/abbreviations.txt"))
	words = word_set!(Shale.arg!("words", "cookbook:words/short_words.txt"))
	accepted_here = accepted_terms!("cookbook:words/accepted_terms.txt")
	project_accepted = Shale.arg!("accepted", "")
	accepted = if project_accepted == "" { accepted_here } else { Set.union(accepted_here, accepted_terms!(project_accepted)) }
	lists = { abbreviations, words, accepted }
	terms_file = Shale.arg!("terms", "")
	terms = if terms_file == "" { Set.empty() } else { word_set!(terms_file) }
	paths = Shale.glob!(Shale.arg!("glob", "**/*.roc"))
	var $report = []
	var $refused = []
	for f in Shale.roc_tree!(paths) {
		if f.err != "" {
			$refused = $refused.append(if Str.starts_with(f.err, f.path) { f.err } else { "${f.path}: ${f.err}" })
		} else {
			src = match Shale.read!(f.path) {
				Found(text) => Str.split_on(text, "\n")
				Missing => []
			}
			declared = declarations_of(f.nodes, src)
			aliases = List.fold(declared, Dict.empty(), |m, d| if d.kind == Type and !(d.is_nominal) { Dict.insert(m, d.name, d.underlying) } else { m })
			for d in declared {
				if d.kind == Value {
					$report = List.concat($report, judge_mutator(f.path, d, aliases, terms))
				}
			}
			if !(house_is_off!({})) {
				$report = List.concat($report, judge_abbreviations(f.path, f.nodes, declared, lists))
			}
			$report = List.concat($report, judge_module_doc!(f.path, f.nodes, declared, src))
		}
	}
	verdict = verdict_of($report, List.len(paths))
	summary = if house_is_off!({}) { "${verdict.summary}; left out as house style: abbreviation" } else { verdict.summary }
	print_findings!(summary, verdict.found)
	if !(List.is_empty($refused)) {
		for r in $refused {
			Shale.print!("refused: ${r}")
		}
		Shale.fail!("find_unclear_names: roc-ast refused ${U64.to_str(List.len($refused))} files (named above), which have no tree to judge; every other file was judged")
	}
	{ summary, found: verdict.found }
}

## The summary, each finding, then the summary again (cookbook/findings.shale).
print_findings! : Str, List(Str) => {}
print_findings! = |summary, found| {
	Shale.print!(summary)
	if !(List.is_empty(found)) {
		for l in found {
			Shale.print!(l)
		}
		Shale.print!(summary)
	}
}

# ── The self-test: the plants, each through the launcher ────────────────

Row : { glob : Str, args : List(Str), counts : List((Str, U64)), says : List(Str), stops : Bool }

plants : Str
plants = "cookbook:roc/plants/find_unclear_names/"

clean : Str -> Row
clean = |glob| { glob: "${plants}${glob}", args: [], counts: [], says: [], stops: Bool.False }

fires : Str, List(Str), List((Str, U64)), List(Str) -> Row
fires = |glob, args, counts, says| { glob: "${plants}${glob}", args, counts, says, stops: Bool.False }

stops : Str, List(Str), List(Str) -> Row
stops = |glob, args, says| { glob: "${plants}${glob}", args, counts: [], says, stops: Bool.True }

rows : List(Row)
rows = [
	fires("pure_mutator.roc", [], [("pure_mutator", 7)], ["pure_mutator.roc:3: pure_mutator: `push_cat` is pure and gives a new `List(U64)`", "`get_speed`"]),
	clean("quiet_pure_mutator.roc"),
	fires("terms.roc", [], [("pure_mutator", 2)], ["`set_cookie_header`", "`set_cookies`"]),
	fires("terms.roc", ["terms=${plants}terms.txt"], [("pure_mutator", 1)], ["`set_cookies`"]),
	stops("terms.roc", ["terms=${plants}no_such.txt"], ["no word list"]),
	fires("abbreviation.roc", [], [("abbreviation", 4)], ["abbreviation.roc:3: abbreviation: `max_vis_cats` abbreviates: vis", "`SunPos` abbreviates: pos", "`dist` abbreviates: dist"]),
	clean("quiet_abbreviation.roc"),
	fires("abbreviation.roc", ["abbreviations=${plants}project_words.txt"], [("abbreviation", 4)], ["`max_vis_cats` abbreviates: vis, cats"]),
	fires("short_words/*.roc", [], [("abbreviation", 5)], ["Tbl.roc:1: abbreviation: `Tbl` abbreviates: tbl", "Tbl.roc:4: abbreviation: `Trk` abbreviates: trk", "Tbl.roc:7: abbreviation: `trk` abbreviates: trk", "`pts` abbreviates: pts", "Tbl.roc:9: abbreviation: `hgt` abbreviates: hgt"]),
	clean("quiet_short_words/*.roc"),
	fires("short_words/*.roc", ["accepted=${plants}project_terms.txt"], [("abbreviation", 3)], ["`Tbl` abbreviates: tbl", "`pts` abbreviates: pts", "`hgt` abbreviates: hgt"]),
	{ ..clean("quiet_short_words/*.roc"), args: ["accepted=${plants}project_terms.txt"] },
	stops("quiet_short_words/*.roc", ["accepted=${plants}no_reason.txt"], ["`qty` has no reason"]),
	stops("quiet_abbreviation.roc", ["abbreviations=${plants}no_such.txt"], ["no word list"]),
	fires("long_abbreviations/*.roc", [], [("abbreviation", 11)], ["Parser.roc:5: abbreviation: `attrs` abbreviates: attrs", "`idxes` abbreviates: idxes", "`vals` abbreviates: vals", "`delim` abbreviates: delim", "`parse_args` abbreviates: args", "`params` abbreviates: params", "`elem_count` abbreviates: elem", "`expr_text` abbreviates: expr", "`stmt_kind` abbreviates: stmt", "`tmpl_name` abbreviates: tmpl"]),
	fires("long_abbreviations/*.roc", ["accepted=${plants}project_args.txt"], [("abbreviation", 9)], ["`attrs` abbreviates: attrs", "`params` abbreviates: params"]),
	clean("quiet_long_abbreviations/*.roc"),
	fires("common_words/*.roc", [], [("abbreviation", 2)], ["`Flw` abbreviates: flw", "`night_dmr` abbreviates: dmr"]),
	clean("quiet_common_words/*.roc"),
	fires("tags.roc", [], [("abbreviation", 5)], ["tags.roc:14: abbreviation: `TmplGone` abbreviates: tmpl", "tags.roc:4: abbreviation: `NoRef` abbreviates: ref", "tags.roc:4: abbreviation: `Ref` abbreviates: ref", "tags.roc:6: abbreviation: `TmplMissing` abbreviates: tmpl", "tags.roc:12: abbreviation: `BadAttr` abbreviates: attr"]),
	clean("quiet_tags.roc"),
	fires("module_doc/*.roc", [], [("module_doc", 5)], ["`NoDoc` has no `##` doc", "`Hashless` has no `##` doc", "`Old`'s top comment says `originally`", "`Sky`'s top comment says `used to`", "Sun.roc:3: module_doc: `Sun`'s doc says `originally`"]),
	clean("quiet_module_doc/*.roc"),
	fires("module_doc_private/*.roc", [], [("module_doc", 2)], ["`Shown` has no `##` doc", "`Silent` has no `##` doc"]),
	stops("broken/*.roc", [], ["refused: "]),
]

## The last line of `out` that isn't one of Shale's `──` lines.
last_said : Str -> Str
last_said = |out| List.fold(lines(Str.trim(out)), "", |last, l| if Str.starts_with(l, "──") or Str.trim(l) == "" { last } else { l })

## What is wrong with one row's run (cookbook/roc/gate_recipe.shale's
## `row_faults`).
row_faults! : Row, { code : I64, out : Str } => List(Str)
row_faults! = |row, r| {
	must_fail = row.stops or !(List.is_empty(row.counts))
	var $faults = []
	if must_fail and r.code == 0 {
		$faults = $faults.append("it exited 0")
	}
	if !(must_fail) and r.code != 0 {
		$faults = $faults.append("it exited ${I64.to_str(r.code)}")
	}
	for check in checks {
		found = List.len(Shale.re_lines!(r.out, "^[^ ]+:[0-9]+: ${check}: "))
		wanted = List.fold(row.counts, 0, |n, pair| if pair.0 == check { pair.1 } else { n })
		if found != wanted {
			$faults = $faults.append("${check} printed ${U64.to_str(found)}, not ${U64.to_str(wanted)}")
		}
	}
	if !(row.stops) and !(Str.contains(last_said(r.out), " files; ")) {
		$faults = $faults.append("its last line is no summary: ${last_said(r.out)}")
	}
	for literal in row.says {
		if !(Str.contains(r.out, literal)) {
			$faults = $faults.append("its output lacks `${literal}`")
		}
	}
	$faults
}

## Each row run as a child of its own, through `launcher`; a check no row
## fires fails too, since it could be blind (`check_plants`).
self_test! : {} => {}
self_test! = |{}| {
	var $wrong = 0
	var $fired = Set.empty()
	for row in rows {
		argv = List.concat(["shale", launcher, "glob=${row.glob}"], row.args)
		r = Shale.run!({ argv, keep: Whole(4000000), timeout_ms: 120000, want: "" })
		faults = row_faults!(row, r)
		for pair in row.counts {
			$fired = Set.insert($fired, pair.0)
		}
		if List.is_empty(faults) {
			Shale.print!("ok   ${row.glob}")
		} else {
			Shale.print!("FAIL ${row.glob}: ${Str.join_with(faults, "; ")}")
			$wrong = $wrong + 1
			Shale.print!(r.out)
		}
	}
	for check in checks {
		if !(Set.contains($fired, check)) {
			Shale.print!("FAIL `${check}` has no plant")
			$wrong = $wrong + 1
		}
	}
	if $wrong > 0 {
		Shale.fail!("find_unclear_names self_test: ${U64.to_str($wrong)} wrong")
	}
	Shale.print!("find_unclear_names self_test: PASS (${U64.to_str(List.len(rows))} rows: each plant gives its findings and a nonzero exit, each clean case none and exit 0)")
}
