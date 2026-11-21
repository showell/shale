## The findings across a run's files, as the lines the recipe prints, with
## its summary and the files Roc's parser refused. Findings stay in the files'
## order and the tree's, as the Shale original printed them, never sorted.
import pf.Shale
import Checks
import lib.Finding
import lib.Text

Report := [].{

	## A run's report: its summary, its findings (each `path:line: check:
	## source line`) and why each refused file was refused.
	Report : {
		summary : Str,
		findings : List(Finding),
		refused : List(Str),
	}

	## The report over `paths`. Under `house=0`, the house style
	## (cookbook/roc/house.shale's `paren_if` and `lambda_if`) is left out.
	make! : List(Str) => Report.Report
	make! = |paths| {
		house = Report.read_house!({})
		var $findings = []
		var $refused = []
		for file in Shale.roc_tree!(paths) {
			if file.err == "" {
				source_lines = Report.read_text!(file.path).split_on("\n")
				for found in Checks.findings_in(file.nodes, source_lines) {
					if is_kept(house, found.check) {
						$findings = $findings.append({
							path: file.path,
							line: found.line,
							check: found.check,
							what: Text.trim(Text.line_at(source_lines, found.line)),
						})
					}
				}
			} else if file.err.starts_with(file.path) {
				$refused = $refused.append(file.err)
			} else {
				$refused = $refused.append("${file.path}: ${file.err}")
			}
		}
		house_note = match house {
			Kept => ""
			LeftOut => "; left out as house style: ${Str.join_with(house_style, ", ")}"
		}
		counts = Finding.counts_of($findings, Checks.names)
		summary = "find_extra_parens (Roc): ${paths.len().to_str()} files; ${counts}${house_note}"
		{ summary, findings: $findings, refused: $refused }
	}

	## Whether `house=0` leaves the house style out (cookbook/roc/house.shale).
	read_house! : {} => [Kept, LeftOut]
	read_house! = |{}| {
		house = Shale.arg!("house", "1")
		if house != "0" and house != "1" {
			Shale.fail!("house must be 1 (the default) or 0, not ${house}")
		}
		if house == "0" {
			LeftOut
		} else {
			Kept
		}
	}

	## A file's text; a file gone since the glob stops the run.
	read_text! : Str => Str
	read_text! = |path|
		match Shale.read!(path) {
			Found(text) => text
			Missing => {
				Shale.fail!("find_extra_parens: no file ${path}")
				""
			}
		}
}

## The checks that are only our house style (cookbook/roc/house.shale).
house_style : List(Str)
house_style = ["paren_if", "lambda_if"]

## Whether a finding of `check` is reported.
is_kept : [Kept, LeftOut], Str -> Bool
is_kept = |house, check|
	match house {
		Kept => Bool.True
		LeftOut => !(house_style.contains(check))
	}
