## The compiler's judgement of a rewrite: one file's open calls tried round
## by round until `roc check` passes (the trial), and the files whose calls
## the types all prove written at once and checked together (the confirm).
## A file ends as it began unless `apply` and its rewrite passed.
import pf.Shale
import Candidates
import Compiler
import Rewritten
import Source
import Tally
import lib.Text

Trial := [].{

	## The Roc that judges, and whether a rewrite that passes is kept.
	Judge : { roc : Str, apply : Bool }

	## A file the confirm writes: its path, its tree and its candidates.
	Proven : { path : Str, file : Source.File, cands : List(Candidates.Candidate) }

	## Tries one file's open candidates, round by round, until `roc check` on
	## `root` passes or nothing is left to try, then runs the test, which
	## must say `baseline`. Answers how many calls were turned, and the
	## candidates, each that stays with its `why_left`.
	try_file! : Trial.Judge, Tally.Sheet, Source.File, List(Candidates.Candidate), Str, Str => { tally : Tally.Sheet, cands : List(Candidates.Candidate), turned : U64 }
	try_file! = |judge, tally, file, cands, root, baseline| {
		var $tally = tally
		fmt_was_clean = if judge.apply {
			$tally = Tally.ran($tally)
			Compiler.is_fmt_clean!(judge.roc, file.path)
		} else {
			Bool.False
		}
		var $cands = cands
		var $round = 0
		while $round < max_rounds {
			$round = $round + 1
			open = Candidates.open_count($cands)
			if open == 0 {
				Shale.write!(file.path, file.text)
				return { tally: $tally, cands: $cands, turned: 0 }
			}
			written = Rewritten.written(file, Candidates.turning($cands))
			$cands = Candidates.with_lines($cands, written)
			Shale.write!(file.path, written.text)
			$tally = Tally.ran({ ..$tally, rounds: $tally.rounds + 1 })
			checked = Compiler.check!(judge.roc, root)
			if checked.ok {
				if fmt_was_clean {
					$tally = Tally.ran($tally)
					Compiler.fmt!(judge.roc, file.path)
				}
				$tally = Tally.ran($tally)
				summary = Compiler.test_summary!(judge.roc, root)
				if summary != baseline {
					why = "roc test changed: ${summary}, was ${baseline}; the file is put back"
					Shale.write!(file.path, file.text)
					return { tally: $tally, cands: Candidates.leave_open($cands, why), turned: 0 }
				}
				$tally = Tally.noted($tally, file, $cands)
				if !(judge.apply) {
					Shale.write!(file.path, file.text)
				}
				return { tally: $tally, cands: $cands, turned: open }
			}
			judged = judge_errors(file, written.line_of_node, checked.errors, $cands)
			$cands = judged.cands
			if !(judged.refused) and (judged.stray != NoStray or checked.errors.is_empty()) {
				why = match judged.stray {
					Stray(stray) => "roc check: ${stray.title} at ${stray.file}:${stray.line.to_str()}, on no rewritten call"
					NoStray => "roc check failed, naming no line: ${Compiler.first_line_said(checked.out)}"
				}
				Shale.write!(file.path, file.text)
				return { tally: $tally, cands: Candidates.leave_open($cands, "${why}; the file is put back"), turned: 0 }
			}
			$cands = Candidates.without_lines($cands)
		}
		Shale.write!(file.path, file.text)
		why = "still failing after ${max_rounds.to_str()} rounds; the file is put back"
		{ tally: $tally, cands: Candidates.leave_open($cands, why), turned: 0 }
	}

	## Confirms the files whose open calls are all proven: written with all
	## their calls turned, at once, and judged together by one `roc check` on
	## `root` and one `roc test`. When the check fails, the files its errors
	## name are set aside and the rest written again, up to `max_rounds`
	## checks; an error in no file of theirs (a caller's), a failure naming no
	## line, or a changed test sets them all aside. `confirmed` holds each
	## kept file's count of turned calls, and 0 for a file set aside, which
	## the trial then judges alone.
	confirm! : Trial.Judge, Tally.Sheet, Str, List(Trial.Proven), Str => Tally.Sheet
	confirm! = |judge, tally, root, files, baseline| {
		var $tally = tally
		var $remaining = []
		for proven in files {
			$tally = { ..$tally, confirmed: $tally.confirmed.insert(proven.path, 0) }
			fmt_was_clean = if judge.apply {
				$tally = Tally.ran($tally)
				Compiler.is_fmt_clean!(judge.roc, proven.path)
			} else {
				Bool.False
			}
			$remaining = $remaining.append({ proven, fmt_was_clean, turned: Candidates.open_count(proven.cands) })
		}
		var $round = 0
		while $round < max_rounds {
			$round = $round + 1
			if $remaining.is_empty() {
				return $tally
			}
			for one in $remaining {
				Shale.write!(one.proven.path, Rewritten.written(one.proven.file, Candidates.turning(one.proven.cands)).text)
			}
			$tally = Tally.ran({ ..$tally, confirms: $tally.confirms + 1 })
			checked = Compiler.check!(judge.roc, root)
			if checked.ok {
				for one in $remaining {
					if one.fmt_was_clean {
						$tally = Tally.ran($tally)
						Compiler.fmt!(judge.roc, one.proven.path)
					}
				}
				$tally = Tally.ran($tally)
				passed = Compiler.test_summary!(judge.roc, root) == baseline
				for one in $remaining {
					if passed {
						$tally = Tally.noted($tally, one.proven.file, one.proven.cands)
					}
					if !(passed) or !(judge.apply) {
						Shale.write!(one.proven.path, one.proven.file.text)
					}
					if passed {
						$tally = { ..$tally, confirmed: $tally.confirmed.insert(one.proven.path, one.turned) }
					}
				}
				return $tally
			}
			for one in $remaining {
				Shale.write!(one.proven.path, one.proven.file.text)
			}
			named = |one| checked.errors.any(|error| Text.basename(one.proven.path) == error.file)
			names_another = |error| !($remaining.any(|one| Text.basename(one.proven.path) == error.file))
			if checked.errors.is_empty() or checked.errors.any(names_another) {
				return $tally
			}
			$remaining = $remaining.drop_if(named)
		}
		$tally
	}

	## How many checks a file may take: each round leaves out the calls the
	## compiler refused, and the next may find more that their errors hid.
	max_rounds : U64
	max_rounds = 6
}

## A failed round's errors laid on the file's turned calls. An error on no
## turned call is a stray, and counts only in a round that refused no call:
## the compiler may complain where a refused call's value is used, and the
## next round, without that call, shows whether the stray was its.
judge_errors : Source.File, Dict(U64, U64), List(Candidates.CheckError), List(Candidates.Candidate) -> { cands : List(Candidates.Candidate), refused : Bool, stray : [Stray(Candidates.CheckError), NoStray] }
judge_errors = |file, line_of_node, errors, cands| {
	var $cands = cands
	var $refused = Bool.False
	var $stray = NoStray
	own = Text.basename(file.path)
	for error in errors {
		if error.file == own {
			blamed = Candidates.blame(file, line_of_node, error, $cands)
			$cands = blamed.cands
			if blamed.hit {
				$refused = Bool.True
			} else if $stray == NoStray {
				$stray = Stray(error)
			}
		} else if $stray == NoStray {
			$stray = Stray(error)
		}
	}
	{ cands: $cands, refused: $refused, stray: $stray }
}
