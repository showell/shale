## The `unpaired_conversion` check: inside a type's braces, a function named
## for a noun that gives the type, such as `Distance.metres : F64 ->
## Distance`, when the same type has `to_<noun>`. `Distance.to_metres` turns
## a distance into metres, so the way back reads `Distance.from_metres`, and
## a bare `metres` beside it leaves the reader to guess which way it goes. A
## constructor with no `to_` partner, such as `Clock.at_step`, is no finding.
import lib.Declarations
import lib.Finding

Conversion := [].{

	## Each function that gives its own type and has a `to_` partner of its
	## name in the same type, a finding, in the order of `values`.
	findings : List(Declarations.ValueDeclared) -> List(Finding)
	findings = |values| {
		names = Set.from_list(values.map(member_key))
		is_unpaired = |value| {
			gives_its_type = value.is_function and value.result_type == value.owner
			partner = member_key({ ..value, name: "to_${value.name}" })
			value.owner != "" and gives_its_type and names.contains(partner)
		}
		values.keep_if(is_unpaired).map(unpaired_finding)
	}
}

## A type's member, by its file, its type and its name.
member_key : Declarations.ValueDeclared -> Str
member_key = |value| "${value.path} ${value.owner}.${value.name}"

## The finding of a conversion into the type that isn't named `from_`.
unpaired_finding : Declarations.ValueDeclared -> Finding
unpaired_finding = |value| {
	owner = value.owner
	name = value.name
	beside = "`${owner}.${name}` gives its own type beside `${owner}.to_${name}`"
	{
		path: value.path,
		line: value.line,
		check: "unpaired_conversion",
		what: "${beside}: name it `from_${name}`, the way back",
	}
}
