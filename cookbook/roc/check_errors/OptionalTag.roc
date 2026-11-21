## The `optional_tag` check: a tag named like an optional (`Some`, `None`,
## `Nothing`; cookbook/words/optionals.txt lists them), matched by its whole
## name. A type that names one beside a tag with a payload is a finding at
## the tag's line; with no payload there, the union is a mode with two ways
## (`[Words, None]`). A tag in an expression or a pattern is a finding once a
## file, at its first use, only when no type in the files checked names it.
import Forest
import lib.Finding
import lib.Text

OptionalTag := [].{

	## A finding with the tag it names, which the self-test holds to the
	## finding's line.
	Found : { finding : Finding, tag : Str }

	## The findings of the types, in the order of their tags, then those of
	## the uses no type names, by path and tag.
	judge : Forest, Set(Str) -> List(OptionalTag.Found)
	judge = |forest, optionals| {
		var $found = []
		var $in_types = Set.empty()
		var $first_uses = Dict.empty()
		$at : U64
		var $at = 0
		while $at < Forest.count(forest) {
			$at = $at + 1
			at = $at
			kind = Forest.kind(forest, at)
			path = Forest.path(forest, at)
			if kind == "ty-tag-union" {
				tags = Forest.union_tags(forest, at)
				has_payload = tags.any(|tag| Forest.kind(forest, tag) == "ty-apply")
				for tag in tags {
					name = Forest.tag_name(forest, tag)
					if optionals.contains(name) {
						$in_types = $in_types.insert(name)
						if has_payload {
							line = Forest.line_number(forest, tag)
							$found = $found.append(in_type({ path, line, tag: name }))
						}
					}
				}
			} else if kind == "e-tag" or kind == "p-tag" {
				name = Forest.raw(forest, at)
				key = "${path}|${name}"
				if optionals.contains(name) and !($first_uses.contains(key)) {
					use = { path, line: Forest.line_number(forest, at), tag: name }
					$first_uses = $first_uses.insert(key, use)
				}
			}
		}
		for key in Text.sort_bytewise($first_uses.keys()) {
			match $first_uses.get(key) {
				Ok(use) =>
					if !($in_types.contains(use.tag)) {
						$found = $found.append(unnamed_use(use))
					}
				Err(_) => {}
			}
		}
		$found
	}
}

## Where an optional tag is, and which.
Place : { path : Str, line : U64, tag : Str }

## An optional named by a type beside a tag with a payload.
in_type : Place -> OptionalTag.Found
in_type = |place| {
	why = "tag what the value is, and its absence by why (`[Latest(Message), NoMessage]`)"
	found(place, "`${place.tag}` names an optional: ${why}")
}

## An optional used where no type in the files read names it.
unnamed_use : Place -> OptionalTag.Found
unnamed_use = |place| {
	why = "tag the value by what it is, and its absence by why"
	found(place, "`${place.tag}` names an optional, and no type in the files read says what it is: ${why}")
}

found : Place, Str -> OptionalTag.Found
found = |place, what| {
	finding: { path: place.path, line: place.line, check: "optional_tag", what },
	tag: place.tag,
}
