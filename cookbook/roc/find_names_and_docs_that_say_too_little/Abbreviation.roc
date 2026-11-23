## The `abbreviation` check: a name with a word short for another, read from
## every name a file gives or says (the launcher's header says the rule).
import lib.Declarations
import lib.Finding
import lib.Text
import lib.Tree

Abbreviation := [].{

	## The word lists the check reads: the abbreviations, the English words
	## of two to four letters, and the accepted terms.
	Lists : { abbreviations : Set(Str), words : Set(Str), accepted : Set(Str) }

	## The findings of the file at `path`, a name once a file.
	judge : Str, List(Tree.Node), Declarations, Abbreviation.Lists -> List(Finding)
	judge = |path, tree, declared, lists| {
		var $seen = Set.empty()
		var $report = []
		for given in names_given(path, tree, declared) {
			if !(Set.contains($seen, given.name)) {
				$seen = Set.insert($seen, given.name)
				found = Text.name_words(given.name).keep_if(|w| is_abbreviated(w, lists))
				if !(List.is_empty(found)) {
					$report = $report.append({ path, line: given.line, check: "abbreviation", what: "`${given.name}` abbreviates: ${Str.join_with(found, ", ")}" })
				}
			}
		}
		$report
	}
}

## Whether a short word stands as it is: an English word, an accepted term,
## or a letter and an axis (`dx`, `vy`).
is_plain_short_word : Str, Abbreviation.Lists -> Bool
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
is_short_abbreviation : Str, Abbreviation.Lists -> Bool
is_short_abbreviation = |w, lists| {
	bytes = Str.to_utf8(w)
	n = List.len(bytes)
	if n < 2 or n > 4 or !(List.all(bytes, Text.is_small_letter)) {
		Bool.False
	} else {
		!(is_plain_short_word(w, lists))
	}
}

## The stems a word may be the plural of (`shapes`: `shape`, `shap`; `tries`:
## `trie`, `tri`, `try`), none for a word ending as no plural does.
plural_stems : Str -> List(Str)
plural_stems = |w| {
	n = w.count_utf8_bytes()
	no_plural = ["ss", "us", "is", "as", "os"].any(|e| Str.ends_with(w, e))
	if n < 3 or !(Str.ends_with(w, "s")) or no_plural {
		return []
	}
	one = [Text.bytes_of(w, { start: 0, count: n - 1 })]
	two = if Str.ends_with(w, "es") and n > 3 {
		one.append(Text.bytes_of(w, { start: 0, count: n - 2 }))
	} else {
		one
	}
	if Str.ends_with(w, "ies") and n > 4 {
		two.append("${Text.bytes_of(w, { start: 0, count: n - 3 })}y")
	} else {
		two
	}
}

## Whether a stem is a whole word, so its plural is plain.
is_whole_stem : Str, Abbreviation.Lists -> Bool
is_whole_stem = |stem, lists|
	if Set.contains(lists.accepted, stem) {
		Bool.True
	} else if stem.count_utf8_bytes() <= 4 {
		is_plain_short_word(stem, lists)
	} else {
		!(Set.contains(lists.abbreviations, stem))
	}

## Whether a word of a name is an abbreviation (the launcher's header says
## the rule).
is_abbreviated : Str, Abbreviation.Lists -> Bool
is_abbreviated = |word, lists| {
	letters = Str.to_utf8(word).keep_if(|b| Text.is_small_letter(b) or Text.is_digit(b))
	whole = Str.from_utf8_lossy(letters)
	if Set.contains(lists.accepted, whole) {
		return Bool.False
	}
	if Set.contains(lists.abbreviations, whole) {
		return Bool.True
	}
	var $end = List.len(letters)
	while $end > 0 and Text.is_digit(List.get(letters, $end - 1) ?? 0) {
		$end = $end - 1
	}
	w = Str.from_utf8_lossy(List.take_first(letters, $end))
	if Set.contains(lists.accepted, w) {
		return Bool.False
	}
	if Set.contains(lists.abbreviations, w) {
		return Bool.True
	}
	if w.count_utf8_bytes() <= 4 and is_plain_short_word(w, lists) {
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
tag_named : List(Tree.Node), U64 -> Str
tag_named = |tree, at| {
	n = Tree.node_at(tree, at)
	if n.kind == "p-tag" or n.kind == "e-tag" {
		return Tree.last_part(Tree.attribute(n, "raw"))
	}
	if n.kind != "ty" or n.parent == 0 {
		return ""
	}
	holder = Tree.node_at(tree, n.parent)
	if holder.kind == "tags" {
		Tree.attribute(n, "name")
	} else if holder.kind == "ty-apply" and List.first(holder.kids) == Ok(at) and Tree.node_at(tree, holder.parent).kind == "tags" {
		Tree.attribute(n, "name")
	} else {
		""
	}
}

## The names a file gives or says, with the line each appears on: the
## module's, what it declares in file order, then its parameters, locals and
## tags.
names_given : Str, List(Tree.Node), Declarations -> List({ name : Str, line : U64 })
names_given = |path, tree, declared| {
	var $out = if Tree.is_app(tree) {
		[]
	} else {
		[{ name: Tree.module_name(path), line: 1 }]
	}
	for d in declared_in_file_order(declared) {
		$out = $out.append({ name: d.name, line: d.line })
	}
	var $i = 0
	for n in tree {
		$i = $i + 1
		if n.kind == "p-ident" {
			$out = $out.append({ name: Tree.attribute(n, "raw"), line: n.line })
		}
		tag = tag_named(tree, $i)
		if tag != "" and tag != "Ok" and tag != "Err" {
			$out = $out.append({ name: tag, line: n.line })
		}
	}
	$out
}

## What the file declares, its types, fields and values together, in file
## order: by the place of each one's node.
declared_in_file_order : Declarations -> List({ place : U64, name : Str, line : U64 })
declared_in_file_order = |declared| {
	types = declared.types.map(|d| { place: d.place, name: d.name, line: d.line })
	fields = declared.fields.map(|d| { place: d.place, name: d.name, line: d.line })
	values = declared.values.map(|d| { place: d.place, name: d.name, line: d.line })
	types.concat(fields).concat(values).sort_with(
		|first, second|
			if first.place < second.place {
				Before
			} else if first.place > second.place {
				After
			} else {
				Same
			},
	)
}
