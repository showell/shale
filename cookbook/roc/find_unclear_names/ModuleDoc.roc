## The `module_doc` check: a module with no `##` doc, or a top comment or a
## doc above its type that tells history (the launcher's header says the
## rule). A package's or platform's main.roc says which modules it keeps to
## itself, so it reads files through the platform.
import pf.Shale
import lib.Declarations
import lib.Finding
import lib.Text
import lib.Tree

ModuleDoc := [].{

	## The findings of the file at `path`, none for an app.
	judge! : Str, List(Tree.Node), Declarations, List(Str) => List(Finding)
	judge! = |path, tree, declared, src| {
		if Tree.is_app(tree) {
			return []
		}
		name = Tree.module_name(path)
		type_line = List.fold(
			declared.types,
			0,
			|at, d| if d.name == name and d.owner == "" {
				d.line
			} else {
				at
			},
		)
		if type_line == 0 {
			return []
		}
		top = opening_comment(src)
		above_type = if type_line > 1 {
			Str.trim(Text.line_at(src, type_line - 1))
		} else {
			""
		}
		opens_with_doc = Str.starts_with(List.first(top) ?? "", "##")
		has_doc = opens_with_doc or (type_line > 1 and Str.starts_with(above_type, "##"))
		kept_to_itself = (
			!(List.is_empty(top)) or Str.starts_with(
				Str.trim(
					Text.line_at(
						src,
						if type_line > 1 {
							type_line - 1
						} else {
							1
						},
					),
				),
				"#",
			)
		) and is_private_module!(path)
		var $report = []
		if !(has_doc) and !(kept_to_itself) {
			$report = $report.append({ path, line: 1, check: "module_doc", what: "`${name}` has no `##` doc saying what it is for" })
		}
		history = history_word!(top)
		if history != "" {
			$report = $report.append({ path, line: 1, check: "module_doc", what: "`${name}`'s top comment says `${history}`: say what the module does, and leave history to git" })
		}
		doc = comment_above(src, type_line)
		doc_history = history_word!(doc.lines)
		if doc.first > 1 and doc_history != "" {
			$report = $report.append({ path, line: doc.first, check: "module_doc", what: "`${name}`'s doc says `${doc_history}`: say what the module does, and leave history to git" })
		}
		$report
	}
}

## The history word in the comment lines, or `""`. Each line's leading `#`s
## go before the lines are joined, so a phrase wrapped across two lines reads
## as one.
history_word! : List(Str) => Str
history_word! = |comment| {
	var $words = []
	for l in comment {
		$words = $words.append(Str.trim(Shale.re_replace!(l, "^#+", "")))
	}
	match Shale.re_find!(Str.with_ascii_lowercased(Str.join_with($words, " ")), "\\b(originally|used to|emitted|ported)\\b") {
		Found(m) => List.first(m.groups) ?? ""
		NoMatch => ""
	}
}

## The comment lines just above line `at`, trimmed, and the line they start
## on (`at` when there are none).
comment_above : List(Str), U64 -> { lines : List(Str), first : U64 }
comment_above = |src, at| {
	var $first = at
	while $first > 1 and Str.starts_with(Str.trim(Text.line_at(src, $first - 1)), "#") {
		$first = $first - 1
	}
	{ lines: List.sublist(src, { start: $first - 1, len: at - $first }).map(Str.trim), first: $first }
}

## The comment lines that open a file, trimmed.
opening_comment : List(Str) -> List(Str)
opening_comment = |src| {
	var $out = []
	var $going = Bool.True
	for l in src {
		if $going and Str.starts_with(Str.trim(l), "#") {
			$out = $out.append(Str.trim(l))
		} else {
			$going = Bool.False
		}
	}
	$out
}

## A file's text with its comment lines left out.
code_of : Str -> Str
code_of = |text| Str.join_with(Text.lines(text).keep_if(|l| !(Str.starts_with(Str.trim(l), "#"))), "\n")

## The names in the list between `[` and `]` of `text`.
bracketed_names : Str -> List(Str)
bracketed_names = |text|
	Str.split_on(Text.between(text, "[", "]") ?? "", ",").map(Str.trim).keep_if(|m| m != "")

## What a folder's `main.roc` exposes, when it is a package's or a
## platform's (cookbook/roc/headers.shale's `exposed_by_folder`).
exposed_by_folder! : Str => { is_package : Bool, modules : List(Str) }
exposed_by_folder! = |dir| {
	main = if dir == "" or dir == "." {
		"main.roc"
	} else {
		"${dir}/main.roc"
	}
	none = { is_package: Bool.False, modules: [] }
	match Shale.read!(main) {
		Missing => none
		Found(whole) => {
			text = code_of(whole)
			word = match Shale.re_find!(text, "[A-Za-z_]+") {
				Found(m) => m.text
				NoMatch => ""
			}
			if word == "package" {
				{ is_package: Bool.True, modules: bracketed_names(text) }
			} else if word == "platform" {
				{ is_package: Bool.True, modules: bracketed_names("${Text.between(text, "exposes", "]") ?? ""}]") }
			} else {
				none
			}
		}
	}
}

## Whether a file is a module its package keeps to itself.
is_private_module! : Str => Bool
is_private_module! = |path| {
	exposed = exposed_by_folder!(Text.dirname(path))
	exposed.is_package and !(List.contains(exposed.modules, Tree.module_name(path))) and Text.basename(path) != "main.roc"
}
