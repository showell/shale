## What hunt_names' `phrase_name` passes as a maker
## (cookbook/roc/declared.shale's `reads_as_maker`): a constructor that gives its own module's type, a
## conversion (`to_*`, `into_*`, and a `from_*` that takes or gives its
## module's type, or sits in a module with no type of its own), and a
## changed copy (`with_*`, `without_*` giving its first argument's type).
import lib.Text
import lib.Tree
import Facts
import Reading
import TypeText

Makers := [].{

	## What a definition's type says: whether it is a function, its first
	## argument's type, what it gives, whether its type is known at all, and
	## the note a finding adds when the type is the checker's.
	Signature : {
		is_function : Bool,
		first : Str,
		result : Str,
		is_known : Bool,
		source_note : Str,
	}

	## Whether a function named `words`, whose first word is a preposition,
	## reads as a constructor, a conversion or a changed copy.
	reads_as_maker : Reading.Definition, List(Str), Makers.Signature, Facts.Gathered, Set(Str) -> Bool
	reads_as_maker = |definition, words, signature, facts, prepositions| {
		word = words.first() ?? ""
		if ["into", "to"].contains(word) {
			Bool.True
		} else if ["with", "without"].contains(word) and signature.result == signature.first {
			Bool.True
		} else if word == "from" {
			is_from_conversion(definition, words, signature, facts, prepositions)
		} else {
			constructor_words.contains(word) and is_own_type(definition, TypeText.success_type(signature.result), facts)
		}
	}
}

## The first words a constructor may start with: the ones constructors use in
## safari, roc-chat and roc-markdown (`Clock.at_step`, `Dm.between`,
## `SignedIn.by_session`, `MdText.for_input`, `Name.of_strict`,
## `Directory.with_founder`).
constructor_words : List(Str)
constructor_words = ["at", "between", "by", "for", "of", "with"]

## Whether `written` names the module the definition is in, a type declared
## in its file, or the type it is a member of. Type arguments don't count.
is_own_type : Reading.Definition, Str, Facts.Gathered -> Bool
is_own_type = |definition, written, facts| {
	head = TypeText.type_head(written)
	if head == "" {
		return Bool.False
	}
	parts = head.split_on(".")
	first_part = parts.first() ?? ""
	last_part = parts.last() ?? ""
	if parts.len() == 1 and facts.type_names.contains("${definition.path}|${first_part}") {
		Bool.True
	} else {
		first_part == Tree.module_name(definition.path) or first_part == definition.owner or last_part == definition.owner
	}
}

## Whether `written` names its module's own type anywhere in it: each run
## that starts with a capital at a word's start.
mentions_own_type : Reading.Definition, Str, Facts.Gathered -> Bool
mentions_own_type = |definition, written, facts|
	capital_runs(written).any(|run| is_own_type(definition, run, facts))

## Each run of name bytes (`A-Z`, `a-z`, `0-9`, `_`, `.`) that starts with a
## capital no word byte stands before, left to right.
capital_runs : Str -> List(Str)
capital_runs = |text| {
	bytes = text.to_utf8()
	var $runs = []
	$at : U64
	var $at = 0
	while $at < bytes.len() {
		byte = bytes.get($at) ?? 0
		before = if $at == 0 {
			' '
		} else {
			bytes.get($at - 1) ?? ' '
		}
		if Text.is_capital(byte) and !(is_word_byte(before)) {
			run = TypeText.type_head(Str.from_utf8_lossy(bytes.drop_first($at)))
			$runs = $runs.append(run)
			$at = $at + run.count_utf8_bytes()
		} else {
			$at = $at + 1
		}
	}
	$runs
}

is_word_byte : U8 -> Bool
is_word_byte = |byte| Text.is_capital(byte) or Text.is_small_letter(byte) or Text.is_digit(byte) or byte == '_'

## Whether the file at `path` declares one type, `[].{ ... }`, which holds
## the module's functions and has no values of its own.
has_no_type_of_its_own : Str, Facts.Gathered -> Bool
has_no_type_of_its_own = |path, facts|
	match facts.type_kinds.get(path) ?? [] {
		[is_holder] => is_holder
		_ => Bool.False
	}

## Whether a function named `words`, whose first word is `from`, reads as a
## conversion.
is_from_conversion : Reading.Definition, List(Str), Makers.Signature, Facts.Gathered, Set(Str) -> Bool
is_from_conversion = |definition, words, signature, facts, prepositions| {
	gives = TypeText.success_type(signature.result)
	if mentions_own_type(definition, signature.first, facts) or mentions_own_type(definition, gives, facts) {
		Bool.True
	} else {
		second = words.get(1) ?? ""
		signature.is_known and has_no_type_of_its_own(definition.path, facts) and words.len() > 1 and !(prepositions.contains(second))
	}
}
