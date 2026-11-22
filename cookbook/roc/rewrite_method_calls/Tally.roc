## What a run has done so far: the programs it started, its checks, the
## files the confirm kept, the rewrites that passed (for what they leave the
## sweep), the kinds of type the types found, and the roots they were read
## of.
import Candidates
import Judging
import Rewritten
import Source

Tally := [].{

	## A rewrite that passed: the text before and after it, before any `roc
	## fmt`, and the lines after it where a turned call that spanned several
	## lines now sits on one.
	Rewrite : { before : Str, after : Str, joined : List(U64) }

	## The run's sheet.
	Sheet : {
		programs_used : U64,
		rounds : U64,
		confirms : U64,
		confirmed : Dict(Str, U64),
		rewrites : Dict(Str, Tally.Rewrite),
		kind_counts : Judging.KindCounts,
		typed_roots : List(Str),
	}

	## Nothing done yet.
	empty : Tally.Sheet
	empty = {
		programs_used: 0,
		rounds: 0,
		confirms: 0,
		confirmed: Dict.empty(),
		rewrites: Dict.empty(),
		kind_counts: [],
		typed_roots: [],
	}

	## One program more.
	ran : Tally.Sheet -> Tally.Sheet
	ran = |tally| { ..tally, programs_used: tally.programs_used + 1 }

	## The rewrite of the file with the candidates `turned` turned, by a
	## passing trial or confirm, or under `trials=0` the proven calls: noted
	## when it changes the text.
	noted : Tally.Sheet, Source.File, List(Candidates.Candidate) -> Tally.Sheet
	noted = |tally, file, turned| {
		turning = Candidates.turning(turned)
		written = Rewritten.written(file, turning)
		var $joined = []
		for cand in turned {
			if turning.contains(cand.node) {
				lines = written.turned_lines.get(cand.node) ?? { from_line: cand.from_line, to_line: cand.to_line }
				node = Source.node_at(file, cand.node)
				spanned_lines = Source.line_of_offset(file, node.from) < Source.line_of_offset(file, node.to - 1)
				if lines.from_line == lines.to_line and spanned_lines {
					$joined = $joined.append(lines.from_line)
				}
			}
		}
		if written.text == file.text {
			tally
		} else {
			rewrite = { before: file.text, after: written.text, joined: $joined }
			{ ..tally, rewrites: tally.rewrites.insert(file.path, rewrite) }
		}
	}
}
