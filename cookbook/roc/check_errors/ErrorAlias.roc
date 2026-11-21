## The `error_alias` check: a function's result is a `Try` whose error is an
## alias of tags (`ReadError : [Missing, Failed]`, or a module's
## `Accounts.Unreadable`) instead of the tags written in place. The alias is
## looked up in the files checked, through up to 8 aliases of aliases: first
## among the file's own declarations, then by its qualified name, which a
## file's top-level types also have by the file's module name (`Access.Answer`
## for `Answer` in Access.roc). Only top-level and module-member annotations
## are read. An error declared outside the files checked can't be judged, and
## is counted by name for the summary.
import Forest
import lib.Finding
import lib.Tree

ErrorAlias := [].{

	## The findings, in the order of the annotations and of the errors inside
	## each result, and how often each error declared outside the files was
	## named.
	judge : Forest -> { findings : List(Finding), unjudged : Dict(Str, U64) }
	judge = |forest| {
		declared = declarations_of(forest)
		var $findings = []
		var $unjudged = Dict.empty()
		$at : U64
		var $at = 0
		while $at < Forest.count(forest) {
			$at = $at + 1
			annotation = $at
			signature = Forest.kid(forest, annotation, 1)
			is_judged = Forest.kind(forest, annotation) == "s-type-anno" and is_top_level(forest, annotation)
			if is_judged and Forest.kind_or_none(forest, signature) == "ty-fn" {
				result = Forest.kids(forest, signature).last() ?? 0
				last = Forest.last_descendant(forest, result)
				$inside : U64
				var $inside = result
				while $inside <= last {
					error = try_error(forest, $inside)
					error_name = if error > 0 {
						Forest.name(forest, error)
					} else {
						""
					}
					if error_name != "" and Forest.kind(forest, error) == "ty" {
						path = Forest.path(forest, annotation)
						match error_kind(forest, declared, { path, name: error_name, hops: 0 }) {
							Union => {
								$findings = $findings.append(alias_finding(forest, annotation, error_name))
							}
							Unknown => {
								counted = ($unjudged.get(error_name) ?? 0) + 1
								$unjudged = $unjudged.insert(error_name, counted)
							}
							Other => {}
						}
					}
					$inside = $inside + 1
				}
			}
		}
		{ findings: $findings, unjudged: $unjudged }
	}
}

## Every type declared in the files checked, by its qualified name (and a
## file's top-level ones by the module's name too), and by its file's path
## and bare name: the place of what it is declared as.
Declared : { qualified : Dict(Str, U64), in_file : Dict(Str, U64) }

declarations_of : Forest -> Declared
declarations_of = |forest| {
	var $qualified = Dict.empty()
	var $in_file = Dict.empty()
	$at : U64
	var $at = 0
	while $at < Forest.count(forest) {
		$at = $at + 1
		name = declared_name(forest, $at)
		if name != "" {
			definition = Forest.kid(forest, $at, 2)
			qualified = nested_name(forest, $at)
			path = Forest.path(forest, $at)
			$qualified = $qualified.insert(qualified, definition)
			if qualified == name {
				$qualified = $qualified.insert("${Tree.module_name(path)}.${name}", definition)
			}
			$in_file = $in_file.insert("${path}|${name}", definition)
		}
	}
	{ qualified: $qualified, in_file: $in_file }
}

## The kinds of node above a top-level or member annotation.
top_level_kinds : Set(Str)
top_level_kinds = Set.from_list(["file", "statements", "s-type-decl", "associated"])

## Whether every node above the one at `at` is a file, its statements, a type
## declaration, or a type's associated block.
is_top_level : Forest, U64 -> Bool
is_top_level = |forest, at| {
	var $above = Forest.parent(forest, at)
	while $above > 0 {
		if !(top_level_kinds.contains(Forest.kind(forest, $above))) {
			return Bool.False
		}
		$above = Forest.parent(forest, $above)
	}
	Bool.True
}

## The name the type declaration at `at` declares, `""` if it is not one.
declared_name : Forest, U64 -> Str
declared_name = |forest, at|
	if Forest.kind(forest, at) != "s-type-decl" or Forest.kids(forest, at).len() < 2 {
		""
	} else {
		Forest.name(forest, Forest.kid(forest, at, 1))
	}

## The declaration's name qualified by the declarations around it
## (`Stored.Problem`).
nested_name : Forest, U64 -> Str
nested_name = |forest, at| {
	var $names = [declared_name(forest, at)]
	var $above = Forest.parent(forest, at)
	while $above > 0 {
		around = declared_name(forest, $above)
		if around != "" {
			$names = List.prepend($names, around)
		}
		$above = Forest.parent(forest, $above)
	}
	Str.join_with($names, ".")
}

builtin_types : Set(Str)
builtin_types = Set.from_list([
	"Str",
	"Bool",
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
	"List",
	"Dict",
	"Set",
	"Box",
])

## What an error type is: an alias of tags, a type declared outside the
## files checked, or anything else (a record, a builtin, an empty union).
ErrorKind : [Union, Unknown, Other]

## The kind of the error type `name` named in `path`, followed through `hops`
## aliases so far, at most 8.
error_kind : Forest, Declared, { path : Str, name : Str, hops : U64 } -> ErrorKind
error_kind = |forest, declared, named| {
	found = match declared.in_file.get("${named.path}|${named.name}") {
		Ok(at) => Ok(at)
		Err(_) => declared.qualified.get(named.name)
	}
	if builtin_types.contains(named.name) {
		Other
	} else {
		match found {
			Err(_) => Unknown
			Ok(at) => {
				kind = Forest.kind(forest, at)
				aliased = Forest.name(forest, at)
				if kind == "ty-tag-union" {
					if Forest.union_tags(forest, at).is_empty() {
						Other
					} else {
						Union
					}
				} else if kind == "ty" and aliased != "" and named.hops < 8 {
					error_kind(forest, declared, { ..named, name: aliased, hops: named.hops + 1 })
				} else {
					Other
				}
			}
		}
	}
}

## The error of a `Try(ok, error)` type at `at`, 0 for any other node.
try_error : Forest, U64 -> U64
try_error = |forest, at|
	if Forest.kind(forest, at) != "ty-apply" or Forest.kids(forest, at).len() != 3 {
		0
	} else if Forest.name(forest, Forest.kid(forest, at, 1)) == "Try" {
		Forest.kid(forest, at, 3)
	} else {
		0
	}

alias_finding : Forest, U64, Str -> Finding
alias_finding = |forest, annotation, error_name| {
	fixes = "list the tags it can return in place, or leave the error to inference (`_`)"
	{
		path: Forest.path(forest, annotation),
		line: Forest.line_number(forest, annotation),
		check: "error_alias",
		what: "`${Forest.name(forest, annotation)}` fails with `${error_name}`, a shared alias of tags: ${fixes}",
	}
}
