## The word lists hunt_names judges names by, read from their one home each:
## cookbook/words/ (verbs, participles, the vague and container words,
## prepositions, namespaces, Roc's own words and the numbers, list names,
## world verbs, cited sources), a project's `terms=` file, the house style's
## checks from cookbook/roc/house.shale and the methods Roc calls by name
## from cookbook/roc/headers.shale. Each refusal is worded as
## cookbook/roc/declared.shale's readers word it.
import pf.Shale
import lib.Text

Words := [].{

	## A project's terms: each term's element type (`""` for none) and its
	## words, by the term.
	Terms : {
		lists : Dict(Str, Str),
		words : Dict(Str, List(Str)),
	}

	## Every table the checks read.
	Lists : {
		verbs : Set(Str),
		participles : Set(Str),
		ed_verbs : Set(Str),
		vague : Set(Str),
		containers : Set(Str),
		prepositions : Set(Str),
		world_verbs : Set(Str),
		namespaces : Dict(Str, Set(Str)),
		taken : Dict(Str, Str),
		list_names : Dict(Str, Str),
		terms : Words.Terms,
		fixed_names : Set(Str),
	}

	## The tables, read in the Shale original's order, and the verbs held to
	## reading as no participle. `terms_file` is `""` for none.
	read! : Str => Words.Lists
	read! = |terms_file| {
		verbs = Words.word_set!("cookbook:words/verbs.txt")
		participles = Words.word_set!("cookbook:words/participles.txt")
		ed_verbs = Words.word_set!("cookbook:words/ed_verbs.txt")
		vague = Words.word_set!("cookbook:words/vague.txt")
		containers = Words.word_set!("cookbook:words/containers.txt")
		prepositions = Words.word_set!("cookbook:words/prepositions.txt")
		namespaces = namespace_instructions!("namespaces.txt")
		taken = taken_word_kinds!("taken.txt")
		list_names = list_name_kinds!("list_names.txt")
		world_verbs = Words.word_set!("cookbook:words/world_verbs.txt")
		terms = project_terms!(terms_file)
		for word in word_list_lines!("cookbook:words/verbs.txt") {
			if Words.reads_as_participle(word, participles, ed_verbs) {
				Shale.fail!("hunt_names: `${word}` is in cookbook/words/verbs.txt and reads as a participle, which would make its effects states")
			}
		}
		fixed_names = language_fixed_names!({})
		{ verbs, participles, ed_verbs, vague, containers, prepositions, world_verbs, namespaces, taken, list_names, terms, fixed_names }
	}

	## A list of one word a line, as a set. A word listed twice, or a line of
	## two words, stops the run.
	word_set! : Str => Set(Str)
	word_set! = |path| {
		if !(Shale.exists!(path)) {
			Shale.fail!("no word list ${path}")
		}
		var $set = Set.empty()
		for word in word_list_lines!(path) {
			if word.contains(" ") or $set.contains(word) {
				Shale.fail!("${path}: `${word}` is no single new word")
			}
			if word != "" {
				$set = $set.insert(word)
			}
		}
		$set
	}

	## Whether a word reads as a past participle: an irregular one of
	## participles.txt, or a word past three letters ending in `ed` that
	## ed_verbs.txt doesn't list.
	reads_as_participle : Str, Set(Str), Set(Str) -> Bool
	reads_as_participle = |word, participles, ed_verbs|
		if participles.contains(word) {
			Bool.True
		} else {
			word.count_utf8_bytes() > 3 and word.ends_with("ed") and !(ed_verbs.contains(word))
		}

	## The checks cookbook/roc/house.shale names as our house style for
	## hunt_names, its one home.
	house_style! : {} => List(Str)
	house_style! = |{}| {
		text = read_text!("cookbook:roc/house.shale")
		match Shale.re_find!(text, "\n  hunt_names = \\{([^}]*)\\}") {
			Found(matched) => Shale.re_find_all!(matched.groups.first() ?? "", "\"([^\"]+)\"").map(|quoted| quoted.groups.first() ?? "")
			NoMatch => {
				Shale.fail!("hunt_names: cookbook/roc/house.shale's HOUSE_CHECKS names no hunt_names")
				[]
			}
		}
	}
}

## A file's text, or a stop naming it.
read_text! : Str => Str
read_text! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => {
			Shale.fail!("no word list ${path}")
			""
		}
	}

## The lines of a word list, any note after `--` cut off, trimmed.
word_list_lines! : Str => List(Str)
word_list_lines! = |path| Text.lines(read_text!(path)).map(|line| Text.trim(Text.before_first(line, "--")))

## namespaces.txt: a prefix a line, then the namespace's own instructions
## (`port in out`), as the set of each prefix's instructions.
namespace_instructions! : Str => Dict(Str, Set(Str))
namespace_instructions! = |file| {
	var $by_prefix = Dict.empty()
	for line in word_list_lines!("cookbook:words/${file}") {
		match Text.words(line) {
			[prefix, .. as instructions] => {
				if $by_prefix.contains(prefix) {
					Shale.fail!("hunt_names: cookbook/words/${file}: `${prefix}` twice")
				}
				$by_prefix = $by_prefix.insert(prefix, Set.from_list(instructions))
			}
			[] => {}
		}
	}
	$by_prefix
}

## taken.txt: a word a line, then its kind, `roc` or `cardinal`.
taken_word_kinds! : Str => Dict(Str, Str)
taken_word_kinds! = |file| {
	var $kinds = Dict.empty()
	for line in word_list_lines!("cookbook:words/${file}") {
		said = Text.words(line)
		if !(said.is_empty()) {
			match said {
				[word, kind] if (kind == "roc" or kind == "cardinal") and !($kinds.contains(word)) => {
					$kinds = $kinds.insert(word, kind)
				}
				_ => Shale.fail!("hunt_names: cookbook/words/${file}: `${Str.join_with(said, " ")}` is no new word with its kind, `roc` or `cardinal`")
			}
		}
	}
	$kinds
}

## list_names.txt: a word a line, then its kind, `many`, `one`, `after`,
## `accumulator` or `adjective`.
list_name_kinds! : Str => Dict(Str, Str)
list_name_kinds! = |file| {
	known_kinds = ["many", "one", "after", "accumulator", "adjective"]
	var $kinds = Dict.empty()
	for line in word_list_lines!("cookbook:words/${file}") {
		said = Text.words(line)
		if !(said.is_empty()) {
			match said {
				[word, kind] if known_kinds.contains(kind) and !($kinds.contains(word)) => {
					$kinds = $kinds.insert(word, kind)
				}
				_ => Shale.fail!("hunt_names: cookbook/words/${file}: `${Str.join_with(said, " ")}` is no new word with its kind, `many`, `one`, `after`, `accumulator` or `adjective`")
			}
		}
	}
	$kinds
}

## A project's own terms, none for `""`: a term a line, its words joined by
## `_`, then after a `:` the element type of the list it names.
project_terms! : Str => Words.Terms
project_terms! = |path| {
	var $terms = { lists: Dict.empty(), words: Dict.empty() }
	if path == "" {
		return $terms
	}
	if !(Shale.exists!(path)) {
		Shale.fail!("hunt_names: terms=${path}: no such file")
	}
	for line in word_list_lines!(path) {
		if line != "" {
			parts = line.split_on(":")
			term = Text.trim(parts.first() ?? "")
			element = Text.trim(parts.get(1) ?? "")
			is_bad_element = element != "" and !(Shale.re_match!(element, "^[A-Z][A-Za-z0-9]*$"))
			is_bad_term = !(Shale.re_match!(term, "^[a-z][a-z0-9]*(_[a-z0-9]+)*$"))
			if parts.len() > 2 or is_bad_term or is_bad_element or $terms.lists.contains(term) {
				Shale.fail!("hunt_names: ${path}: `${line}` is no new term, `word` or `word: Type`")
			}
			$terms = {
				lists: $terms.lists.insert(term, element),
				words: $terms.words.insert(term, term.split_on("_")),
			}
		}
	}
	$terms
}

## The methods Roc calls by name, from their one home,
## cookbook/roc/headers.shale's LANGUAGE_FIXED_NAMES.
language_fixed_names! : {} => Set(Str)
language_fixed_names! = |{}| {
	text = read_text!("cookbook:roc/headers.shale")
	match Shale.re_find!(text, "\nconst LANGUAGE_FIXED_NAMES = \\{([^\n]*)\\}\n") {
		Found(matched) => {
			entries = Shale.re_find_all!(matched.groups.first() ?? "", "([a-z_!]+)\"?\\]? = true")
			Set.from_list(entries.map(|entry| entry.groups.first() ?? ""))
		}
		NoMatch => {
			Shale.fail!("hunt_names: cookbook/roc/headers.shale names no LANGUAGE_FIXED_NAMES")
			Set.empty()
		}
	}
}
