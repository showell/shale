## The `bare_beside_unit` check: a field that is a bare number in one record
## and a unit in another, a unit being a nominal type built on a number and
## declared in the files read. It means a retyping stopped halfway. Each bare
## field is a finding. A field named by a preposition (`from`, `to`, `at`) or
## by a word the table gives `any` (`id`, where a host's number and a user's
## id are two things) is not judged, because that word fits any two things.
import lib.Declarations
import lib.Finding
import lib.Tree
import Words

BareBesideUnit := [].{

	## Each bare field beside a unit field of its name, in the order declared.
	findings : Declarations, Words -> List(Finding)
	findings = |declared, words| {
		units = unit_names(declared.types)
		var $unit_fields = Dict.empty()
		for field in declared.fields {
			if units.contains(Tree.last_part(field.written_type)) {
				$unit_fields = $unit_fields.insert(field.name, field)
			}
		}
		is_judged = |field|
			bare_numbers.contains(field.written_type)
				and !(words.prepositions.contains(field.name))
				and !(Words.promises_nothing(words, field.name))
		var $found = []
		for field in declared.fields {
			match $unit_fields.get(field.name) {
				Ok(typed) =>
					if is_judged(field) {
						$found = $found.append(bare_field(field, typed))
					}
				Err(_) => {}
			}
		}
		$found
	}
}

## The numbers Roc builds in, which a unit is built on.
bare_numbers : Set(Str)
bare_numbers = Set.from_list([
	"U8",
	"U16",
	"U32",
	"U64",
	"U128",
	"I8",
	"I16",
	"I32",
	"I64",
	"I128",
	"F32",
	"F64",
	"Dec",
])

## The nominal types built on a number, by name.
unit_names : List(Declarations.TypeDeclared) -> Set(Str)
unit_names = |types| {
	is_unit = |declared| declared.is_nominal and bare_numbers.contains(declared.underlying)
	Set.from_list(types.keep_if(is_unit).map(|declared| declared.name))
}

## The finding of a bare field, naming the unit field of its name.
bare_field : Declarations.FieldDeclared, Declarations.FieldDeclared -> Finding
bare_field = |field, typed| {
	bare = "the field `${field.name}` is a bare `${field.written_type}` here"
	unit_place = "${typed.path}:${typed.line.to_str()}"
	{
		path: field.path,
		line: field.line,
		check: "bare_beside_unit",
		what: "${bare} and a `${typed.written_type}` at ${unit_place}",
	}
}
