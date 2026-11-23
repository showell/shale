## The `family` check. Modules whose names end in the same word form a family
## when there are three or more of them (`CatSighting`, `TreeSighting`,
## `RailSighting`). A public name that more than half of a family's members
## have is the family's, and each member without it is a finding, unless the
## member names the missing name in its file's opening comment or the doc
## above its type, to say why it differs. A reader who knows one member then
## finds the others' names without looking.
import lib.Declarations
import lib.Finding
import lib.Headers
import lib.Text
import lib.Tree

Family := [].{

	## A module: a file that declares a type named for itself at the top
	## level, with the names of that type's members, and the text its doc
	## gives (the file's opening comment and the comment above its type).
	Module : {
		name : Str,
		path : Str,
		line : U64,
		members : Set(Str),
		doc : Str,
	}

	## A file as the check reads it: its path, what it declares, and its lines.
	FileRead : {
		path : Str,
		declared : Declarations,
		source_lines : List(Str),
	}

	## The file's module, when it declares one with any member that isn't a
	## method Roc calls by name.
	module_of : Family.FileRead -> [IsModule(Family.Module), NoModule]
	module_of = |file| {
		name = Tree.module_name(file.path)
		line = module_line(file.declared.types, name)
		is_member = |value| value.owner == name and !(Headers.language_fixed_names.contains(value.name))
		members = Set.from_list(file.declared.values.keep_if(is_member).map(|value| value.name))
		if line == 0 or members.is_empty() {
			NoModule
		} else {
			opening = Str.join_with(opening_comment(file.source_lines), " ")
			doc = "${opening} ${doc_above(file.source_lines, line)}"
			IsModule({
				name,
				path: file.path,
				line,
				members,
				doc,
			})
		}
	}

	## Each member that lacks a name most of its family has, a finding.
	findings : List(Family.Module) -> List(Finding)
	findings = |modules| {
		var $families = Dict.empty()
		for member in modules {
			match last_word(member.name) {
				Ok(word) => {
					$families = $families.insert(word, ($families.get(word) ?? []).append(member))
				}
				Err(OneWord) => {}
			}
		}
		$families
			.to_list()
			.sort_with(|(a, _), (b, _)| Text.byte_order(a, b))
			.map(|(word, members)| family_findings(word, members))
			.join()
	}
}

## The smallest family.
smallest_family : U64
smallest_family = 3

## The line of the last top-level type named `name`, the module's own, or 0.
module_line : List(Declarations.TypeDeclared), Str -> U64
module_line = |types, name| {
	var $line = 0
	for declared in types {
		if declared.name == name and declared.owner == "" {
			$line = declared.line
		}
	}
	$line
}

## How many of `members` have each name, by name.
holder_counts : List(Family.Module) -> Dict(Str, U64)
holder_counts = |members| {
	var $counts = Dict.empty()
	for member in members {
		for name in member.members.to_list() {
			counted = match $counts.get(name) {
				Ok(count) => count + 1
				Err(_) => 1
			}
			$counts = $counts.insert(name, counted)
		}
	}
	$counts
}

## The findings of the family whose names end in `word`.
family_findings : Str, List(Family.Module) -> List(Finding)
family_findings = |word, members| {
	if members.len() < smallest_family {
		return []
	}
	var $found = []
	counts = holder_counts(members).to_list().sort_with(|(a, _), (b, _)| Text.byte_order(a, b))
	for (name, holder_count) in counts {
		if holder_count * 2 > members.len() {
			for member in members {
				if !(member.members.contains(name)) and !(member.doc.contains("`${name}`")) {
					gap = {
						member,
						name,
						holder_count,
						member_count: members.len(),
						word,
					}
					$found = $found.append(missing_name(gap))
				}
			}
		}
	}
	$found
}

## A member without a name its family has: the name, how many of the
## family's members have it, and the family's last word.
Gap : {
	member : Family.Module,
	name : Str,
	holder_count : U64,
	member_count : U64,
	word : Str,
}

## The finding of a member without its family's `name`.
missing_name : Gap -> Finding
missing_name = |gap| {
	who = "`${gap.member.name}` has no `${gap.name}`"
	counted = "${gap.holder_count.to_str()} of the ${gap.member_count.to_str()}"
	which = "which ${counted} `*${gap.word}` modules have"
	advice = "spell it alike, or name it in the module's doc to say why not"
	{
		path: gap.member.path,
		line: gap.member.line,
		check: "family",
		what: "${who}, ${which}: ${advice}",
	}
}

## The last word of a name of two or more words.
last_word : Str -> Try(Str, [OneWord])
last_word = |name|
	match Text.name_words(name) {
		[_, .., last] => Ok(last)
		_ => Err(OneWord)
	}

## The comment lines that open a file, trimmed.
opening_comment : List(Str) -> List(Str)
opening_comment = |source_lines| {
	var $out = []
	for line in source_lines {
		trimmed = Text.trim(line)
		if !(trimmed.starts_with("#")) {
			return $out
		}
		$out = $out.append(trimmed)
	}
	$out
}

## The comment lines directly above line `line`, nearest first, lowercased
## and joined: a declaration's doc.
doc_above : List(Str), U64 -> Str
doc_above = |source_lines, line| {
	var $parts = []
	var $above = line - 1
	while $above >= 1 {
		trimmed = Text.trim(Text.line_at(source_lines, $above))
		if !(trimmed.starts_with("#")) {
			return Str.join_with($parts, " ")
		}
		$parts = $parts.append(Text.lower(trimmed))
		$above = $above - 1
	}
	Str.join_with($parts, " ")
}
