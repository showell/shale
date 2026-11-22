## What the rewrites left for a sweep to fix in the same pass, which the
## recipe reports and does not fix: each import that a file used before its
## rewrite and no longer uses after it, by the cookbook's one home of that
## rule (lib's Imports, find_unused_imports'), and each line wider than 100
## columns where a turned call joined its lines (a turn alone only shortens a
## line). A line is named by its number in the rewritten text, before any
## `roc fmt`.
import pf.Shale
import Tally
import lib.Imports
import lib.Text

Sweep := [].{

	## The findings, a `path:line: made_unused_import: why` or
	## `made_long_line` each, and the count of each kind.
	Made : { lines : List(Str), unused : U64, long : U64 }

	## What the rewrites on the sheet made. Roc's parser reads files, so both
	## texts of each are written to a scratch folder, removed after.
	made! : Tally.Sheet => Sweep.Made
	made! = |tally| {
		rewrites = tally.rewrites.to_list().sort_with(|(first, _), (second, _)| Text.byte_order(first, second))
		if rewrites.is_empty() {
			return { lines: [], unused: 0, long: 0 }
		}
		dir = Sweep.fresh_dir!("made_by_rewrites")
		var $files = []
		$number : U64
		var $number = 0
		for (_, rewrite) in rewrites {
			$number = $number + 1
			folder = "${dir}/${$number.to_str()}"
			Shale.mkdir!(folder)
			Shale.write!("${folder}/before.roc", rewrite.before)
			Shale.write!("${folder}/after.roc", rewrite.after)
			$files = $files.concat(["${folder}/before.roc", "${folder}/after.roc"])
		}
		var $trees = Dict.empty()
		for tree in Shale.roc_tree!($files) {
			if tree.err != "" {
				Shale.fail!("rewrite_method_calls: roc-ast refused a rewritten text (${tree.path}): ${tree.err}")
			}
			$trees = $trees.insert(tree.path, tree.nodes)
		}
		var $made = { lines: [], unused: 0, long: 0 }
		$number = 0
		for (path, rewrite) in rewrites {
			$number = $number + 1
			folder = "${dir}/${$number.to_str()}"
			before = Imports.unused_imports($trees.get("${folder}/before.roc") ?? [], rewrite.before.split_on("\n"))
			was_unused = Set.from_list(before.map(|unused| unused.label))
			after_lines = rewrite.after.split_on("\n")
			for unused in Imports.unused_imports($trees.get("${folder}/after.roc") ?? [], after_lines) {
				if !(was_unused.contains(unused.label)) {
					said = "${path}:${unused.line.to_str()}: made_unused_import: ${unused.label} was used only by the calls the rewrite turned: delete the line"
					$made = { ..$made, unused: $made.unused + 1, lines: $made.lines.append(said) }
				}
			}
			var $seen = Set.empty()
			for number in rewrite.joined.sort_with(
				|first, second| if first < second {
					Before
				} else if first > second {
					After
				} else {
					Same
				},
			) {
				text_line = Text.line_at(after_lines, number)
				width = columns_of(text_line)
				if width > long_line and !($seen.contains(number)) {
					$seen = $seen.insert(number)
					shown = Text.trim(Text.bytes_of(text_line, { start: 0, count: 120 }))
					said = "${path}:${number.to_str()}: made_long_line: ${width.to_str()} columns after the rewrite joined a call's lines: stack it (wrap_long_lines.shale): ${shown}"
					$made = { ..$made, long: $made.long + 1, lines: $made.lines.append(said) }
				}
			}
		}
		Shale.remove_tree!(dir)
		$made
	}

	## A folder of the run's own below `scratch:`, `<prefix>.XXXXXX`, so two
	## runs at once never share one (Shale's `fresh_dir`); the caller removes
	## it.
	fresh_dir! : Str => Str
	fresh_dir! = |prefix| {
		made = Shale.run!({
			argv: ["mktemp", "-d", "${Shale.real!("scratch:")}/${prefix}.XXXXXX"],
			keep: Tail(2),
			timeout_ms: 10000,
			want: "",
		})
		if made.code != 0 {
			Shale.fail!("rewrite_method_calls: no scratch folder: ${made.out}")
		}
		"scratch:${Text.basename(Text.trim(made.out))}"
	}
}

## The width past which a line is long, as check_stacking's `width`.
long_line : U64
long_line = 100

## A line's width, a tab counting four columns.
columns_of : Str -> U64
columns_of = |text_line| {
	bytes = text_line.to_utf8()
	bytes.len() + 3 * bytes.count_if(|byte| byte == '\t')
}
