## check_families, in Roc on Shale's platform (roc_platform/README.md):
## modules that look alike but are spelled unlike, and one type name with two
## meanings. A reader who knows `TreeSighting.trees_in_view` looks for
## `RailSighting.rails_in_view`, and one who knows `Cat.Pose` reads
## `Geom.Pose` as the same thing, so a family spelled unlike or a twin of
## another shape costs every reader a wrong guess. Its launcher,
## cookbook/roc/check_families.shale, is how it is run, and its header says
## the rules, `family` and `twin_name`. check_goldens.shale, beside this,
## holds its output on the plants to its goldens, line for line.
##
##   cd <repo> && shale cookbook:roc/check_families.shale roles=tools/type_roles.txt
##   shale cookbook:roc/check_families.shale self_test=1
##
## The arguments: `glob` (`**/*.roc`), `roles` (a file of type names each
## module may declare for itself) and `self_test` (`self_test=1` runs the
## plants, each through the launcher).
##
## This root globs the files (`Shale.glob!`), has Roc's parser read them
## (`Shale.roc_tree!`) and each one's text (`Shale.read!`), reads the roles
## file, and prints the verdict (`Shale.print!`, `Shale.fail!`). Its modules
## are beside it, a module a check (Family, Twin) and SelfTest; what they read
## (Tree, Declarations, Text) and Finding are the cookbook's Roc library,
## cookbook/roc/lib/.
## budget: steps=800000000 arena_bytes=2000000000 output_bytes=4000000 programs=100
app [main!] { lib: "../lib/main.roc", pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import lib.Declarations
import lib.Finding
import lib.Text
import lib.Tree
import Family
import SelfTest
import Twin

main! : {} => {}
main! = |{}| {
	if Shale.arg!("self_test", "") == "1" {
		SelfTest.run!(checks)
	} else {
		run_check!({})
	}
}

## What one parsed file gives the checks.
ParsedFile : { is_app : Bool, read : Family.FileRead }

## The check over the glob's files: the verdict printed, then a stop when a
## file didn't parse, or when there are findings, the summary the last line
## a caller reads.
run_check! : {} => {}
run_check! = |{}| {
	paths = Shale.glob!(Shale.arg!("glob", "**/*.roc"))
	var $files = []
	var $refused = []
	for file in Shale.roc_tree!(paths) {
		if file.err == "" {
			source_lines = read_text!(file.path).split_on("\n")
			read = {
				path: file.path,
				declared: Declarations.from_file({ path: file.path, nodes: file.nodes, source_lines }),
				source_lines,
			}
			$files = $files.append({ is_app: Tree.is_app(file.nodes), read })
		} else if file.err.starts_with(file.path) {
			$refused = $refused.append(file.err)
		} else {
			$refused = $refused.append("${file.path}: ${file.err}")
		}
	}
	findings = run_checks!($files)
	found = findings.sort_with(Finding.order).map(Finding.line_of)
	summary = summary_of(findings, paths.len())
	print_findings!(summary, found)
	if !($refused.is_empty()) {
		for refusal in $refused {
			Shale.print!("refused: ${refusal}")
		}
		refusals = "roc-ast refused ${$refused.len().to_str()} files (named above)"
		why = "which have no tree to judge; every other file was judged"
		Shale.fail!("check_families: ${refusals}, ${why}")
	}
	if !(found.is_empty()) {
		Shale.fail!(summary)
	}
}

## Both checks over the files that parsed, the roles file read for
## `twin_name`.
run_checks! : List(ParsedFile) => List(Finding)
run_checks! = |files| {
	family_findings = Family.findings(modules_of(files))
	roles_file = Shale.arg!("roles", "")
	roles = if roles_file == "" {
		Set.empty()
	} else {
		read_word_set!(roles_file)
	}
	twin_files = files
		.keep_if(|file| !(file.is_app))
		.map(|file| { path: file.read.path, types: file.read.declared.types })
	family_findings.concat(Twin.findings(twin_files, roles))
}

## The modules among the files read, in their order.
modules_of : List(ParsedFile) -> List(Family.Module)
modules_of = |files| {
	var $modules = []
	for file in files {
		match Family.module_of(file.read) {
			IsModule(found) => {
				$modules = $modules.append(found)
			}
			NoModule => {}
		}
	}
	$modules
}

## The file's text. A file the parser read and the read finds gone stops the
## run.
read_text! : Str => Str
read_text! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => {
			Shale.fail!("check_families: ${path} went missing during the check")
			""
		}
	}

## A list of one word a line, any note after `--` cut off, as a set
## (cookbook/roc/declared.shale's `word_set`). A missing file, a word listed
## twice, or a line of two words stops the run.
read_word_set! : Str => Set(Str)
read_word_set! = |path| {
	text = match Shale.read!(path) {
		Found(found) => found
		Missing => {
			Shale.fail!("no word list ${path}")
			""
		}
	}
	var $words = Set.empty()
	for line in Text.lines(text) {
		word = Text.trim(Text.before_first(line, "--"))
		if word.contains(" ") or $words.contains(word) {
			Shale.fail!("${path}: `${word}` is no single new word")
		}
		if word != "" {
			$words = $words.insert(word)
		}
	}
	$words
}

## The checks, in the order the summary counts them.
checks : List(Str)
checks = ["family", "twin_name"]

## The counts, `check_families (Roc): N files; family N, twin_name N`.
summary_of : List(Finding), U64 -> Str
summary_of = |findings, file_count| {
	"check_families (Roc): ${file_count.to_str()} files; ${Finding.counts_of(findings, checks)}"
}

## The summary, each finding, then the summary again (cookbook/findings.shale):
## the first copy survives a cut output, and a gate reads the last line.
print_findings! : Str, List(Str) => {}
print_findings! = |summary, finding_lines| {
	Shale.print!(summary)
	if !(finding_lines.is_empty()) {
		for line in finding_lines {
			Shale.print!(line)
		}
		Shale.print!(summary)
	}
}
