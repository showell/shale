## hunt_names' checks judged over what the whole read gathered, once every
## file is read (the Shale original's naming_passes.shale):
## `preposition_split`, from
## the trailing prepositions each file puts on its stems; `container` and a
## field's `short_name`, from the type and field names; `where_name`, from a
## file's `where` clauses; and the census of effects' first words that
## `words=1` prints. The launcher's header says what each check finds.
import lib.Finding
import lib.Text
import lib.Tree
import Facts
import Judge
import Reading
import Words

Passes := [].{

	## An effect's first word in the census: how many effects start with it
	## and what it says.
	Counted : { count : U64, verdict : Str }

	## What `preposition_split` finds in the definitions, in the order of
	## their files' paths and stems. `keywords` are each file's declared
	## keywords, by path.
	preposition_splits : List(Reading.Definition), Words.Lists, Dict(Str, List(Str)) -> List(Finding)
	preposition_splits = |definitions, lists, keywords| {
		var $groups = Dict.empty()
		var $shapes = Dict.empty()
		var $conventions = Set.empty()
		for definition in definitions {
			words = definition.name.drop_suffix("!").split_on("_")
			preposition = words.last() ?? ""
			is_judged = !(lists.fixed_names.contains(definition.name)) and !(definition.is_paired)
			if is_judged and words.len() > 1 and lists.prepositions.contains(preposition) {
				stem = Str.join_with(words.drop_last(1), "_")
				key = "${definition.path}|${stem}"
				group = $groups.get(key) ?? { path: definition.path, line: definition.line, stem, uses: Dict.empty() }
				if definition.is_annotation and definition.is_function_type {
					shape_key = "${definition.path}|${preposition}|${definition.first}|${definition.result}"
					stems = ($shapes.get(shape_key) ?? Set.empty()).insert(stem)
					$shapes = $shapes.insert(shape_key, stems)
					if stems.len() >= convention_stems {
						$conventions = $conventions.insert("${definition.path}|${preposition}")
					}
				}
				uses = if group.uses.contains(preposition) {
					group.uses
				} else {
					group.uses.insert(preposition, { name: definition.name, arity: definition.arity })
				}
				$groups = $groups.insert(key, { ..group, uses })
			}
		}
		Text.sort_bytewise($groups.keys()).map(
			|key| match $groups.get(key) {
				Ok(group) => split_findings(group, $conventions, keywords)
				Err(_) => []
			},
		).join()
	}

	## What `short_name` and `container` find in one type or field name.
	container_findings : Facts.Named, Words.Lists -> List(Finding)
	container_findings = |named, lists| {
		report = |check, what| { path: named.path, line: named.line, check, what }
		is_container_word = |word| lists.vague.contains(word) or lists.containers.contains(word)
		is_only_containers = !(named.words.is_empty()) and named.words.all(is_container_word)
		is_owned = named.words.any(|word| doc_says_whose(named.doc, word))
		# A field of a plain value (`entry : F64`) holds nothing a container
		# could.
		is_scalar = named.is_field and !(named.is_function) and scalar_types.contains(named.gives)
		var $found = []
		if named.is_field and named.name.count_utf8_bytes() == 1 and !(named.is_letter_of_set) {
			$found = $found.append(report("short_name", "the field `${named.name}` is one letter, read far from where it is set: name what it holds"))
		}
		if is_only_containers and !(is_owned) and !(is_scalar) {
			what = if named.is_field {
				"the field `"
			} else {
				"the type `"
			}
			$found = $found.append(report("container", "${what}${named.name}` is only container words: name it for what it holds, or say whose it is in its doc (`## A folder's entry`)"))
		}
		$found
	}

	## What `where_name` finds in one parsed file. `fixed_names` are the
	## methods Roc calls by name, which pass.
	where_name_findings : Str, List(Tree.Node), Set(Str) -> List(Finding)
	where_name_findings = |path, nodes, fixed_names| {
		is_where_method = |node| node.kind == "method" and node.parent > 0 and Tree.node_at(nodes, node.parent).kind == "where"
		nodes
			.keep_if(is_where_method)
			.keep_if(
				|node| {
					name = Tree.attribute(node, "name")
					name.drop_suffix("!").split_on("_").len() < 2 and !(fixed_names.contains(name))
				},
			)
			.map(
				|node| {
					name = Tree.attribute(node, "name")
					{ path, line: node.line, check: "where_name", what: "the `where` method `${name}` is one word, and a `where` clause matches by name alone: say what it is for (`scenery_height`, not `height`)" }
				},
			)
	}

	## Each first word the definitions' effects start with, counted, with
	## its verdict where the word first appears.
	census : List(Reading.Definition), Words.Lists -> Dict(Str, Passes.Counted)
	census = |definitions, lists| {
		var $counted = Dict.empty()
		for definition in definitions {
			is_effect = Judge.is_effectful(definition)
			if is_effect and !(definition.is_paired) and !(lists.fixed_names.contains(definition.name)) {
				act = Judge.act_of(definition.name.drop_suffix("!").split_on("_"), is_effect, lists)
				before = $counted.get(act.word) ?? { count: 0, verdict: act.verdict }
				$counted = $counted.insert(act.word, { ..before, count: before.count + 1 })
			}
		}
		$counted
	}

	## The census `words=1` prints: a line a word, the most used first, and
	## the summary's counts.
	census_lines : Dict(Str, Passes.Counted) -> { lines : List(Str), summary : Str }
	census_lines = |counted| {
		rows = counted.to_list().sort_with(
			|(word, row), (other_word, other_row)|
				if row.count > other_row.count {
					Before
				} else if row.count < other_row.count {
					After
				} else {
					Text.byte_order(word, other_word)
				},
		)
		row_lines = rows.map(|(word, row)| "${padded_left(row.count.to_str(), 4)} ${padded_right(word, 12)} ${row.verdict}")
		count_of = |counted_rows| counted_rows.map(|(_, row)| row.count).sum()
		total = count_of(rows)
		by_verdict = |verdict| count_of(rows.keep_if(|(_, row)| row.verdict == verdict))
		verdict_counts = ["verb", "participle", "vague", "asks", "refused"].map(|verdict| "${verdict} ${by_verdict(verdict).to_str()}")
		summary = "${total.to_str()} effects, ${rows.len().to_str()} first words; effects by verdict: ${Str.join_with(verdict_counts, ", ")}"
		{ lines: row_lines, summary }
	}
}

## A stem's first use of each trailing preposition, by the preposition.
Group : {
	path : Str,
	line : U64,
	stem : Str,
	uses : Dict(Str, { name : Str, arity : I64 }),
}

## How many stems of one shape make a preposition a file's convention.
convention_stems : U64
convention_stems = 3

## The pairs of opposite prepositions, sorted and joined by `|`.
opposite_pairs : List(Str)
opposite_pairs = ["after|before", "from|to", "with|without", "over|under"]

## A field of one of these holds one plain value, so `container` passes it.
scalar_types : List(Str)
scalar_types = ["U8", "U16", "U32", "U64", "U128", "I8", "I16", "I32", "I64", "I128", "F32", "F64", "Dec", "Str", "Bool"]

## The finding of one stem's group, if its prepositions split it.
split_findings : Group, Set(Str), Dict(Str, List(Str)) -> List(Finding)
split_findings = |group, conventions, keywords| {
	arities = group.uses.to_list().map(|(_, used)| used.arity).keep_if(|arity| arity >= 0)
	fewest = arities.min() ?? -1
	declared = keywords.get(group.path) ?? []
	split = Text.sort_bytewise(group.uses.keys()).keep_if(
		|preposition| {
			arity = (group.uses.get(preposition) ?? { name: "", arity: -1 }).arity
			is_from_worker = preposition == "from" and fewest >= 0 and arity > fewest
			is_convention = conventions.contains("${group.path}|${preposition}")
			!(is_from_worker) and !(is_convention) and !(declared.contains(preposition))
		},
	)
	names = split.map(|preposition| (group.uses.get(preposition) ?? { name: "", arity: -1 }).name)
	is_opposite_pair = split.len() == 2 and opposite_pairs.contains(Str.join_with(split, "|"))
	if split.len() > 1 and !(is_opposite_pair) {
		what = "`${group.stem}` takes ${split.len().to_str()} prepositions here (${Str.join_with(names, ", ")}): one relation keeps one preposition, or the names say how they differ"
		[{ path: group.path, line: group.line, check: "preposition_split", what }]
	} else {
		[]
	}
}

## Whether a doc says whose a container word is (`a folder's entry`, `a
## listing's entries`), in the word's singular or plural.
doc_says_whose : Str, Str -> Bool
doc_says_whose = |doc, word| {
	plain = [word, "${word}s"]
	from_ies = if word.ends_with("ies") {
		["${word.drop_suffix("ies")}y"]
	} else {
		[]
	}
	to_ies = if word.ends_with("y") {
		["${word.drop_suffix("y")}ies"]
	} else {
		[]
	}
	from_s = if word.ends_with("s") {
		[word.drop_suffix("s")]
	} else {
		[]
	}
	plain.concat(from_ies).concat(to_ies).concat(from_s).any(|form| doc.contains("'s ${form}"))
}

## The text with spaces before it to `width` bytes, kept whole when longer
## (Shale's `pad(text, -width)`).
padded_left : Str, U64 -> Str
padded_left = |text, width| {
	length = text.count_utf8_bytes()
	if length >= width {
		text
	} else {
		"${Str.repeat(" ", width - length)}${text}"
	}
}

## The text with spaces after it to `width` bytes (Shale's `pad(text,
## width)`).
padded_right : Str, U64 -> Str
padded_right = |text, width| {
	length = text.count_utf8_bytes()
	if length >= width {
		text
	} else {
		"${text}${Str.repeat(" ", width - length)}"
	}
}
