## The `name_type` check: a name whose last word is in the project's table has
## a type the table doesn't allow. A function is judged by what it returns,
## and a type by its last part, so `Geom.Distance` is `Distance`. In a name
## that starts with a preposition or a question, the last word isn't the
## thing named (a function from a fraction, a state after two steps, a
## question whether it is a fraction), so it is not judged. `none` means no
## name may end in the word, whatever its type or start, and `any` means the
## word holds no promise at all.
import lib.Declarations
import lib.Finding
import lib.Text
import lib.Tree
import Words

NameType := [].{

	## Each value and field whose name disagrees with the table: a field
	## judged by its own type, a value by what it gives.
	findings : Declarations, Words -> List(Finding)
	findings = |declared, words| {
		named_fields = declared.fields.map(
			|field| {
				path: field.path,
				line: field.line,
				name: field.name,
				type_judged: field.written_type,
			},
		)
		named_values = declared.values.map(
			|value| {
				path: value.path,
				line: value.line,
				name: value.name,
				type_judged: value.result_type,
			},
		)
		named_fields.concat(named_values).map(|named| name_findings(named, words)).join()
	}

}

## A name judged: a value's or a field's, where it is, and the type that
## stands for it (what a function returns, a field's own type).
Named : {
	path : Str,
	line : U64,
	name : Str,
	type_judged : Str,
}

## The finding of `named`, when its type disagrees with the table.
name_findings : Named, Words -> List(Finding)
name_findings = |named, words| {
	name_words = Text.name_words(named.name.drop_suffix("!"))
	type_judged = named.type_judged
	if type_judged == "" or type_judged == "_" {
		return []
	}
	match (name_words.first(), name_words.last()) {
		(Ok(first_word), Ok(last_word)) => {
			is_question = question_words.contains(first_word)
			names_something_else = words.prepositions.contains(first_word) or is_question
			said = { named, last_word }
			match words.promises.get(last_word) {
				Ok(NoNameMay) => [no_name_may(said)]
				Ok(OneOf(allowed)) =>
					if names_something_else or allowed.contains(Tree.last_part(type_judged)) {
						[]
					} else {
						[not_allowed(said, allowed)]
					}
				Ok(AnyType) => []
				Err(_) => []
			}
		}
		_ => []
	}
}

## The first words that ask a question, so the name's last word is not the
## thing named.
question_words : Set(Str)
question_words = Set.from_list(["is", "has", "can", "should"])

## A name judged, and its last word.
Said : { named : Named, last_word : Str }

## The finding of a name ending in a word no name may end in.
no_name_may : Said -> Finding
no_name_may = |said| {
	named = said.named
	ending = "`${named.name}` ends in `${said.last_word}`"
	{
		path: named.path,
		line: named.line,
		check: "name_type",
		what: "${ending}, which no name here may: its type is `${named.type_judged}`",
	}
}

## The finding of a name whose type is none of those the table allows.
not_allowed : Said, List(Str) -> Finding
not_allowed = |said, allowed| {
	named = said.named
	allowed_said = Str.join_with(allowed, "` or `")
	ending = "a name ending `${said.last_word}` is `${allowed_said}` here"
	{
		path: named.path,
		line: named.line,
		check: "name_type",
		what: "`${named.name}` is `${named.type_judged}`, and ${ending}",
	}
}
