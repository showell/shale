## Every finding in a run's files, in the files' order and each file's line
## order, as the Shale original found them: a group on one line that should
## stack, a function type's bare arguments, a stacked group that should sit
## on one line, and a line wider than the width with nothing to stack, which
## is `wide string` when its strings' text is what makes it wide (by Roc's
## parser) and `long` otherwise. The names a file's header dictates, and
## those the `dictated` table names, are no finding for their arguments.
import pf.Shale
import lib.Bytes
import Annotations
import lib.Finding
import lib.Groups
import lib.Headers
import lib.Stacking
import lib.Text
import lib.Tree

Report := [].{

	## What a run judges by: the stacking rule, and the path of the
	## `dictated` table (`""` for none).
	Settings : { rule : Stacking.Rule, dictated : Str }

	## A run's findings, and why each file roc-ast refused was refused.
	Judged : { found : List(Finding), refused : List(Str) }

	## Every finding in the files.
	findings_in! : List(Str), Report.Settings => Report.Judged
	findings_in! = |paths, settings| {
		var $texts = Dict.empty()
		for path in paths {
			$texts = $texts.insert(path, Report.read_text!(path))
		}
		texts = $texts
		strings = string_columns_in!(paths, { texts, width: settings.rule.width })
		headed = dictated_names_in!(paths, texts)
		dictated = with_dictated_table!(headed.names, { paths, table: settings.dictated })
		var $found = []
		for path in paths {
			source_lines = (texts.get(path) ?? "").split_on("\n")
			file = {
				path,
				source_lines,
				string_columns: strings.columns.get(path) ?? Dict.empty(),
				dictated: dictated.get(path) ?? [],
				rule: settings.rule,
			}
			match file_findings(file) {
				Ok(found) => {
					$found = $found.concat(found)
				}
				Err(NoComma(line)) => Shale.fail!("groups: line ${line.to_str()} does not end in its group's trailing comma")
			}
		}
		var $refused = []
		for refusal in strings.refused.concat(headed.refused) {
			if !($refused.contains(refusal)) {
				$refused = $refused.append(refusal)
			}
		}
		{ found: $found, refused: $refused }
	}

	## Why roc-ast refused a file, beginning with its path
	## (cookbook/roc/parsed.shale's `parse_roc_files`).
	refusal_of : Str, Str -> Str
	refusal_of = |path, err|
		if err.starts_with(path) {
			err
		} else {
			"${path}: ${err}"
		}

	## A file's text; a file gone since the glob stops the run.
	read_text! : Str => Str
	read_text! = |path|
		match Shale.read!(path) {
			Found(text) => text
			Missing => {
				Shale.fail!("check_and_fix_group_stacking: no file ${path}")
				""
			}
		}
}

## A file to judge: its path, its lines, the columns its strings' text takes
## up by line number, the names whose arguments something else dictates, and
## the rule.
FileToJudge : {
	path : Str,
	source_lines : List(Str),
	string_columns : Dict(U64, U64),
	dictated : List(Str),
	rule : Stacking.Rule,
}

## What a file's scan knows so far: the function whose annotation ended on
## the line before and was a finding for its arguments (`""` for none),
## whose lambda's arguments are not reported again, and the last line of the
## annotation read last.
Annotated : { name : Str, end : U64 }

## The findings in one file, line by line, then the groups to unstack.
file_findings : FileToJudge -> Try(List(Finding), [NoComma(U64)])
file_findings = |file| {
	rule = file.rule
	reachable = Stacking.without_targets_section(file.source_lines)
	var $found = []
	var $annotated = { name: "", end: 0 }
	$number : U64
	var $number = 0
	for text in file.source_lines {
		$number = $number + 1
		number = $number
		on_line = line_findings(
			file,
			{
				number,
				text,
				reachable: reachable.get(number - 1) ?? "",
				annotated: $annotated,
			},
		)
		$annotated = on_line.annotated
		$found = $found.concat(on_line.found)
	}
	for unstacked in Stacking.groups_to_unstack(file.source_lines, rule)? {
		joined = Stacking.group_on_one_line(file.source_lines, unstacked.group) ?? ""
		$found = $found.append({
			path: file.path,
			line: unstacked.group.line,
			check: "unstack",
			what: "${unstacked.group.tokens.to_str()} tokens in ${Annotations.shortened(Text.trim(joined))}",
		})
	}
	Ok($found)
}

## A line to judge: its number, its text, its text with a `targets:` section
## blanked, and what the scan knew of the annotation before it.
LineToJudge : {
	number : U64,
	text : Str,
	reachable : Str,
	annotated : Annotated,
}

## The findings on one line, and what the scan knows after it.
line_findings : FileToJudge, LineToJudge -> { found : List(Finding), annotated : Annotated }
line_findings = |file, at| {
	rule = file.rule
	bytes = at.text.to_utf8()
	place = { path: file.path, line: at.number }
	before = at.annotated
	follows_annotation = before.name != "" and at.number == before.end + 1
	var $skip_lambda = follows_annotation and Text.trim(at.text).starts_with("${before.name} = |")
	var $annotated = if at.number > before.end {
		{ ..before, name: "" }
	} else {
		before
	}
	data_line = Stacking.is_data_line(at.reachable, rule.width)
	var $found = []
	for group in Groups.groups_on_line(at.reachable).sort_with(by_opening) {
		inner = Str.from_utf8_lossy(Bytes.span(bytes, group.open + 1, group.close - 1))
		whole = Str.from_utf8_lossy(Bytes.span(bytes, group.open, group.close))
		opening = Bytes.byte_at(bytes, group.open)
		if opening == '{' {
			for field in Annotations.record_field_functions(inner) {
				if field.items >= 3 and !(file.dictated.contains(field.name)) {
					arguments = {
						items: field.items,
						shown: Annotations.shortened(field.text),
						text: field.text,
					}
					$found = $found.append(Annotations.function_type_finding(place, arguments))
				}
			}
		}
		if group.items >= 3 and group.fn_type {
			text = Text.before_first(Text.before_first(inner, "->"), "=>")
			arguments = {
				items: group.items,
				shown: Annotations.shortened(whole),
				text,
			}
			$found = $found.append(Annotations.function_type_finding(place, arguments))
		} else if $skip_lambda and opening == '|' {
			$skip_lambda = Bool.False
		} else if !(data_line) {
			are_flags = group.items >= 3 and !(group.trailing) and Annotations.are_all_flags(inner)
			if are_flags or Stacking.should_stack({ line: at.text, group }, rule) {
				advice = if are_flags {
					Annotations.flag_advice
				} else {
					""
				}
				what = "${group.tokens.to_str()} tokens in ${Annotations.shortened(whole)}${advice}"
				$found = $found.append({
					path: file.path,
					line: at.number,
					check: "stack",
					what,
				})
			}
		}
	}
	# A line inside a wrapped annotation starts none.
	if at.number > $annotated.end {
		match Annotations.annotation_arguments(file.source_lines, at.number) {
			Ok(annotation) => {
				$annotated = { ..$annotated, end: annotation.last }
				if annotation.items >= 3 {
					$annotated = { ..$annotated, name: annotation.name }
					if !(file.dictated.contains(annotation.name)) {
						arguments = {
							items: annotation.items,
							shown: annotation.shown,
							text: annotation.text,
						}
						$found = $found.append(Annotations.function_type_finding(place, arguments))
					}
				}
			}
			Err(_) => {}
		}
	}
	if $found.is_empty() and Bytes.width_of(bytes) > rule.width {
		$found = $found.append(wide_line_finding(file, at))
	}
	{ found: $found, annotated: $annotated }
}

## The finding for a line wider than the width with nothing else found on it.
wide_line_finding : FileToJudge, LineToJudge -> Finding
wide_line_finding = |file, at| {
	rule = file.rule
	width = Bytes.line_width(at.text)
	strings = file.string_columns.get(at.number) ?? 0
	without_strings = width.to_i64_wrap() - strings.to_i64_wrap()
	if without_strings <= rule.width.to_i64_wrap() {
		what = "${width.to_str()} columns, ${without_strings.to_str()} without its strings"
		{
			path: file.path,
			line: at.number,
			check: "wide string",
			what,
		}
	} else {
		why = if Text.trim(at.text).starts_with("#") {
			"a comment"
		} else if Stacking.group_to_stack(at.reachable, rule).is_ok() {
			"a group to stack"
		} else {
			"nothing to stack"
		}
		{
			path: file.path,
			line: at.number,
			check: "long",
			what: "${width.to_str()} columns, ${why}",
		}
	}
}

## The columns that strings' text takes up in the files, by Roc's parser,
## by path and line number, and why each file roc-ast refused was refused.
## Only a file with a line wider than the width is parsed, so a tree of short
## lines never needs roc-ast.
string_columns_in! : List(Str),
{ texts : Dict(Str, Str), width : U64 } => {
	columns : Dict(Str, Dict(U64, U64)),
	refused : List(Str),
}
string_columns_in! = |paths, given| {
	wide_paths = paths.keep_if(
		|path| {
			text = given.texts.get(path) ?? ""
			Text.lines(text).any(|line| Bytes.line_width(line) > given.width)
		},
	)
	var $columns = Dict.empty()
	var $refused = []
	if !(wide_paths.is_empty()) {
		for file in Shale.roc_tree!(wide_paths) {
			if file.err == "" {
				source_lines = (given.texts.get(file.path) ?? "").split_on("\n")
				match Stacking.string_columns_on_lines(file.nodes, source_lines) {
					Ok(by_line) => {
						$columns = $columns.insert(file.path, by_line)
					}
					Err(CrossesLine(line)) => Shale.fail!("check_and_fix_group_stacking: ${file.path}: line ${line.to_str()}: a run of string text crosses a line")
				}
			} else {
				$refused = $refused.append(Report.refusal_of(file.path, file.err))
			}
		}
	}
	{ columns: $columns, refused: $refused }
}

## The names the headers of the files dictate, by path, and why each file
## roc-ast refused was refused. Only an app's or a platform's file is parsed,
## so a tree of modules needs no roc-ast for this.
dictated_names_in! : List(Str),
Dict(Str, Str) => {
	names : Dict(Str, List(Str)),
	refused : List(Str),
}
dictated_names_in! = |paths, texts| {
	headed = paths.keep_if(
		|path| {
			word = Headers.header_word(texts.get(path) ?? "")
			word == "app" or word == "platform"
		},
	)
	var $names = Dict.empty()
	var $refused = []
	if !(headed.is_empty()) {
		for file in Shale.roc_tree!(headed) {
			if file.err == "" {
				nodes : List(Tree.Node)
				nodes = file.nodes
				$names = $names.insert(file.path, Headers.header_names(nodes).dictated)
			} else {
				$refused = $refused.append(Report.refusal_of(file.path, file.err))
			}
		}
	}
	{ names: $names, refused: $refused }
}

## The names the `dictated` table adds. Each line of the table is a file's
## path, a function's name, and after ` -- ` who dictates its arguments. The
## path names the file the glob finds with that path or ending `/` and that
## path, so the self-test's copies in scratch are found too. A line whose
## file the glob doesn't find, whose function has no annotation there, or
## that gives no reason stops the run, so the table can't go stale.
with_dictated_table! : Dict(Str, List(Str)),
{
	paths : List(Str),
	table : Str,
} => Dict(Str, List(Str))
with_dictated_table! = |names, given| {
	if given.table == "" {
		return names
	}
	var $names = names
	for line in Text.lines(Report.read_text!(given.table)) {
		trimmed = Text.trim(line)
		if trimmed != "" and !(trimmed.starts_with("#")) {
			groups = match Shale.re_find!(line, "^(\\S+) ([a-z_][A-Za-z0-9_]*!?) -- \\S") {
				Found(found) => found.groups
				NoMatch => []
			}
			(wanted_path, name) = match groups {
				[path_given, name_given, ..] => (path_given, name_given)
				_ => {
					Shale.fail!("check_and_fix_group_stacking: ${given.table} has a line that is not `path name -- reason`: ${line}")
					("", "")
				}
			}
			is_named = |path| path == wanted_path or path.ends_with("/${wanted_path}")
			found = given.paths.keep_if(is_named).last()
			match found {
				Err(_) => Shale.fail!("check_and_fix_group_stacking: ${given.table} names ${wanted_path}, which the glob doesn't find")
				Ok(path) => {
					opening = "${name} :"
					path_lines = Text.lines(Report.read_text!(path))
					annotated = path_lines.any(|text| Text.trim(text).starts_with(opening))
					if !(annotated) {
						Shale.fail!("check_and_fix_group_stacking: ${given.table} names ${name}, which has no annotation in ${path}")
					}
					here = $names.get(path) ?? []
					$names = $names.insert(path, here.append(name))
				}
			}
		}
	}
	$names
}

## Two groups of a line in the order of their opening brackets.
by_opening : Groups.Group, Groups.Group -> [Before, Same, After]
by_opening = |first, second|
	if first.open < second.open {
		Before
	} else if first.open > second.open {
		After
	} else {
		Same
	}
