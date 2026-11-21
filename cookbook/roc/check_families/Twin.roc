## The `twin_name` check: one type name declared in two modules with two
## different shapes, such as `Cat.Pose` and `Geom.Pose`, one word with two
## meanings, so a reader who knows one guesses the other wrong. It is
## reported once, at its first declaration, with the others listed. Twins of
## one shape are copies, as when a package can't import its platform's type,
## so they are no finding. A namespace module (`Collider := [].{ ... }`)
## builds a type and declares none, and so does an alias of the twin
## (`Pose : Geom.Pose`). App files are left out by the caller, because every
## app declares what its platform asks for.
import lib.Declarations
import lib.Finding
import lib.Text

Twin := [].{

	## Where a type is declared, and its shape.
	Place : {
		path : Str,
		line : U64,
		shape : Str,
	}

	## A file's path and the types it declares.
	FileTypes : { path : Str, types : List(Declarations.TypeDeclared) }

	## The twins among the files' types, in the order the files were read.
	## A name in `roles` is each module's own to declare, so it has no twin.
	findings : List(Twin.FileTypes), Set(Str) -> List(Finding)
	findings = |files, roles| {
		is_role = |name| roles.contains(name)
		is_twin = |(name, places)| places.len() > 1 and !(is_role(name)) and has_two_shapes(places)
		files
			.fold(Dict.empty(), with_places)
			.to_list()
			.sort_with(|(a, _), (b, _)| Text.byte_order(a, b))
			.keep_if(is_twin)
			.map(|(name, places)| twin_finding(name, places))
	}
}

## `places_by_name` with the file's types added, each name once a file, at
## its first declaration, and only where the type declares a shape of its
## own.
with_places : Dict(Str, List(Twin.Place)), Twin.FileTypes -> Dict(Str, List(Twin.Place))
with_places = |places_by_name, file| {
	var $places = places_by_name
	var $seen = Set.empty()
	for declared in file.types {
		if !($seen.contains(declared.name)) and !(is_type_elsewhere(declared)) {
			$seen = $seen.insert(declared.name)
			place = {
				path: file.path,
				line: declared.line,
				shape: declared.shape,
			}
			named_so_far = $places.get(declared.name) ?? []
			$places = $places.insert(declared.name, named_so_far.append(place))
		}
	}
	$places
}

## Whether the type declares nothing of its own: a namespace module, or an
## alias of a type of its own name.
is_type_elsewhere : Declarations.TypeDeclared -> Bool
is_type_elsewhere = |declared| {
	shape = declared.shape
	shape == "" or shape == "[]" or shape == declared.name
}

has_two_shapes : List(Twin.Place) -> Bool
has_two_shapes = |places|
	match places {
		[first, .. as rest] => rest.any(|place| place.shape != first.shape)
		[] => Bool.False
	}

## The finding at the name's first place, naming the others.
twin_finding : Str, List(Twin.Place) -> Finding
twin_finding = |name, places|
	match places {
		[first, .. as rest] => {
			others = Str.join_with(rest.map(|place| "${place.path}:${place.line.to_str()}"), ", ")
			{
				path: first.path,
				line: first.line,
				check: "twin_name",
				what: "`${name}` is also declared at ${others}, in another shape",
			}
		}
		[] => crash "check_families: a twin with no place"
	}
