## hunt_names' `abbreviation` check, over the whole tree's top-level names: a
## name with a word of cookbook/words/c_abbreviations.txt whose spelled-out
## word another top-level name of the tree uses is one thing with two
## spellings (`bi_split` beside `builtin_scan`). One finding a file and
## abbreviation, at its first name's line, naming its names, as
## cookbook/c/hunt_names.shale, the Shale original, words it.
import Checks

Abbreviation := [].{

	## The tree's findings, from its names in the order the files and their
	## items gave them, and the table of each abbreviation's word.
	findings : List(Checks.Named), Dict(Str, Str) -> List(Checks.Finding)
	findings = |named, table| {
		spelled = named.fold(Set.empty(), |words, each| each.words.fold(words, |all, word| all.insert(word)))
		var $short = Dict.empty()
		for each in named {
			for word in distinct(each.words) {
				match table.get(word) {
					Ok(full) if spelled.contains(full) => {
						key = "${each.path}|${word}"
						entry = $short.get(key) ?? { path: each.path, line: each.line, word, full, names: [] }
						$short = $short.insert(key, { ..entry, names: entry.names.append(each.name) })
					}
					_ => {}
				}
			}
		}
		$short.to_list().map(|(_, entry)| finding(entry))
	}
}

## One file's abbreviation: where it is first, the word and its spelled-out
## word, and the names that hold it.
Short : { path : Str, line : U64, word : Str, full : Str, names : List(Str) }

## The finding of a file's abbreviation: its first four names, and how many
## more.
finding : Short -> Checks.Finding
finding = |entry| {
	names = distinct(entry.names)
	listed = "`${Str.join_with(names.take_first(4), "`, `")}`"
	shown = if names.len() > 4 {
		"${listed} and ${(names.len() - 4).to_str()} more"
	} else {
		listed
	}
	count = if names.len() == 1 {
		"1 name"
	} else {
		"${names.len().to_str()} names"
	}
	why = "`${entry.word}` for `${entry.full}`, which other names spell out, in ${count} here: ${shown}"
	{ path: entry.path, line: entry.line, check: "abbreviation", why }
}

## Each text once, in the order first seen, as Shale's `unique` gives them.
distinct : List(Str) -> List(Str)
distinct = |texts|
	texts.fold(
		[],
		|seen, text|
			if seen.contains(text) {
				seen
			} else {
				seen.append(text)
			},
	)
