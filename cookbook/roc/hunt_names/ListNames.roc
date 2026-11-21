## What hunt_names' `singular_list` reads of a name whose type says many
## (cookbook/roc/declared.shale's `list_name_finding`): the word that counts,
## whether it reads as a plural, and the names a finding offers instead. A
## list that is no function and is named from a preposition is
## `phrase_name`'s instead, as the launcher's header says.
import lib.Finding
import lib.Text
import lib.Tree
import TypeText
import Words

ListNames := [].{

	## A name whose type may say many: the file and line it is at, the name,
	## what the finding calls it (`function`, `value`, `parameter`, `field`),
	## whether it is a function, its first argument's type and what it gives
	## or holds.
	Subject : {
		path : Str,
		line : U64,
		name : Str,
		kind : Str,
		is_function : Bool,
		first : Str,
		gives : Str,
	}

	## The finding a name whose type says many and whose head word says one
	## makes, if it makes one.
	findings : ListNames.Subject, Words.Lists -> List(Finding)
	findings = |subject, lists| {
		element = match TypeText.list_element(subject.gives) {
			Ok(found) => found
			Err(NoList) => return []
		}
		bare = subject.name.drop_suffix("!")
		kind_of = |word| lists.list_names.get(word) ?? ""
		if element == "U8" or bare.count_utf8_bytes() < 2 or (lists.taken.get(bare) ?? "") == "cardinal" or kind_of(bare) == "accumulator" {
			return []
		}
		words = joined_terms(bare.split_on("_"), lists.terms)
		says = if subject.is_function {
			"gives"
		} else {
			"holds"
		}
		opening = "the ${subject.kind} `${subject.name}` ${says} `${subject.gives}`"
		at = head_word_at(words, lists)
		head = word_at(words, at)
		first_word = word_at(words, 1)
		if head == "list" or reads_as_plural(head, lists) {
			return []
		}
		term_element = lists.terms.lists.get(head)
		match term_element {
			Ok(named) if named != "" and Tree.last_part(TypeText.type_head(element)) == named => return []
			Ok(named) if named == "" and subject.is_function and words.len() == 1 => return []
			_ => {}
		}
		starts_with_act = subject.is_function and lists.verbs.contains(first_word) and kind_of(first_word) != "one"
		is_changed_copy = subject.is_function and subject.first == subject.gives
		if is_changed_copy and words.len() > 1 {
			return []
		}
		if TypeText.element_word(element) != head {
			if kind_of(head) == "many" or Words.reads_as_participle(head, lists.participles, lists.ed_verbs) {
				return []
			}
			if is_changed_copy or (starts_with_act and words.len() == 1) {
				return []
			}
		}
		said = |check, what| [{ path: subject.path, line: subject.line, check, what }]
		if lists.prepositions.contains(first_word) and !(subject.is_function) {
			said("phrase_name", "${opening} and starts with a preposition, `${first_word}`: bare, it hides what it holds; name the thing (`trees_in_view`, not `in_view`)")
		} else if kind_of(head) == "adjective" {
			held = held_plural(element)
			example = if held != "" {
				" (`${head}_${held}`)"
			} else {
				""
			}
			said("singular_list", "${opening} but the word that counts, `${head}`, is an adjective: the name has no noun; say what the list holds${example}")
		} else {
			said("singular_list", "${opening} but reads as one thing, `${head}`: ${honest_list_names(bare, words, at, element)}")
		}
	}
}

## The word at place `at` of `words`, counted from 1.
word_at : List(Str), U64 -> Str
word_at = |words, at| words.get(at - 1) ?? ""

## `words` with each run of words that spells a term joined into one word,
## the longest term first: `two`, `opt`, `stops` gives `two_opt`, `stops`.
joined_terms : List(Str), Words.Terms -> List(Str)
joined_terms = |words, terms| {
	term_words = terms.words.to_list().map(|(_, spelled)| spelled)
	var $joined = []
	$at : U64
	var $at = 0
	while $at < words.len() {
		rest = words.drop_first($at)
		taken = term_words.fold(
			1,
			|longest, spelled| if spelled.len() > longest and rest.take_first(spelled.len()) == spelled {
				spelled.len()
			} else {
				longest
			},
		)
		$joined = $joined.append(Str.join_with(rest.take_first(taken), "_"))
		$at = $at + taken
	}
	$joined
}

## The place of the word of a name that says what it is, counted from 1:
## the one before the first preposition or word list_names.txt calls
## `after`, past the name's first word (`trees_in_view` gives `trees`), else
## the last that is not digits alone (`corner_2` gives `corner`).
head_word_at : List(Str), Words.Lists -> U64
head_word_at = |words, lists| {
	$place : U64
	var $place = 2
	while $place <= words.len() {
		word = word_at(words, $place)
		if lists.prepositions.contains(word) or lists.list_names.get(word) == Ok("after") {
			return $place - 1
		}
		$place = $place + 1
	}
	var $at = words.len()
	while $at > 1 and is_digits(word_at(words, $at)) {
		$at = $at - 1
	}
	$at
}

is_digits : Str -> Bool
is_digits = |word| {
	bytes = word.to_utf8()
	!(bytes.is_empty()) and bytes.all(Text.is_digit)
}

## Whether a word reads as a plural. An `s` doesn't make one in `class`, in a
## word list_names.txt calls `one` (`axis`), or after letters that are no
## word (`pts`); after one letter it does (`xs`).
reads_as_plural : Str, Words.Lists -> Bool
reads_as_plural = |word, lists| {
	if lists.list_names.get(word) == Ok("one") or !(word.ends_with("s")) or word.ends_with("ss") {
		return Bool.False
	}
	stem = word.drop_suffix("s").to_utf8()
	stem.len() == 1 or stem.any(|byte| byte == 'a' or byte == 'e' or byte == 'i' or byte == 'o' or byte == 'u' or byte == 'y')
}

## A word's plural, as English spells most.
plural_of : Str -> Str
plural_of = |word| {
	is_vowel_before_last = match word.to_utf8() {
		[.., before, _] => before == 'a' or before == 'e' or before == 'i' or before == 'o' or before == 'u'
		_ => Bool.False
	}
	if word.ends_with("y") and !(is_vowel_before_last) {
		"${word.drop_suffix("y")}ies"
	} else if ["s", "x", "z", "ch", "sh"].any(|ending| word.ends_with(ending)) {
		"${word}es"
	} else {
		"${word}s"
	}
}

## The plural of what a list of `element` holds, for a name to say it, or
## `""` when the element has no word of its own: a type variable, a
## number, text.
held_plural : Str -> Str
held_plural = |element| {
	held = TypeText.element_word(element)
	starts_capital = match element.to_utf8().first() {
		Ok(byte) => Text.is_capital(byte)
		Err(_) => Bool.False
	}
	if held == "" or !(starts_capital) or plain_elements.contains(element) {
		""
	} else {
		plural_of(held)
	}
}

## The element types that hold no word of their own.
plain_elements : List(Str)
plain_elements = ["U16", "U32", "U64", "I16", "I32", "I64", "F32", "F64", "Dec", "Str", "Bool"]

## The names `singular_list` offers for `bare`, whose head word is at `at`
## in `words`: the plural of what the list holds put after the head, as in
## `herd_cows`, or the head's own plural when the element is that word, a
## number or text; else the name with `_list`.
honest_list_names : Str, List(Str), U64, Str -> Str
honest_list_names = |bare, words, at, element| {
	held = held_plural(element)
	says_what_it_holds = held != "" and TypeText.element_word(element) != word_at(words, at)
	var $plural_words = []
	$place : U64
	var $place = 0
	for word in words {
		$place = $place + 1
		if $place != at {
			$plural_words = $plural_words.append(word)
		} else if says_what_it_holds {
			$plural_words = $plural_words.append(word).append(held)
		} else {
			$plural_words = $plural_words.append(plural_of(word))
		}
	}
	"`${Str.join_with($plural_words, "_")}`, or `${bare}_list`"
}
