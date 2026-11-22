## The recipe's self-test: each plant under
## cookbook/roc/plants/check_vocabulary/ run through the launcher, held to
## its findings, its exit and what it says. It copies
## cookbook/roc/check_families/SelfTest.roc's running and judging of a row,
## on purpose: a row is run through `Shale.run!`, and cookbook/roc/lib/, a
## package, reaches no effect.
import pf.Shale

SelfTest := [].{

	## Each row run as a child of its own, through `launcher`; a check of
	## `checks` no row fires fails too, since it could be blind.
	run! : List(Str) => {}
	run! = |checks| {
		var $wrong = 0.U64
		var $fired = Set.empty()
		for row in rows {
			command = ["shale", launcher, "glob=${row.plant}"].concat(row.arguments)
			result = Shale.run!({
				argv: command,
				keep: Whole(4000000),
				timeout_ms: 120000,
				want: "",
			})
			faults = find_faults!(row, result, checks)
			for (check, _) in row.counts {
				$fired = $fired.insert(check)
			}
			if faults.is_empty() {
				Shale.print!("ok   ${row.plant}")
			} else {
				Shale.print!("FAIL ${row.plant}: ${Str.join_with(faults, "; ")}")
				$wrong = $wrong + 1
				Shale.print!(result.out)
			}
		}
		for check in checks {
			if !($fired.contains(check)) {
				Shale.print!("FAIL `${check}` has no plant")
				$wrong = $wrong + 1
			}
		}
		if $wrong > 0 {
			Shale.fail!("check_vocabulary self_test: ${$wrong.to_str()} wrong")
		}
		row_count = rows.len().to_str()
		proof = "each plant gives its findings and a nonzero exit, each clean case none and exit 0"
		Shale.print!("check_vocabulary self_test: PASS (${row_count} rows: ${proof})")
	}
}

## The launcher, which the self-test runs each plant through.
launcher : Str
launcher = "cookbook:roc/check_vocabulary.shale"

plants : Str
plants = "cookbook:roc/plants/check_vocabulary/"

## The plants' own words file, as the argument that names it.
words : Str
words = "words=${plants}words.txt"

## A plant's run: its files (a glob below `plants`) and its other arguments,
## the findings each check must print, the literals its output must hold,
## and whether it must stop nonzero whatever its findings.
Row : {
	plant : Str,
	arguments : List(Str),
	counts : List((Str, U64)),
	says : List(Str),
	stops : Bool,
}

## What a plant is run with and must give: its findings, and what its
## output must hold.
Wanted : {
	plant : Str,
	arguments : List(Str),
	counts : List((Str, U64)),
	says : List(Str),
}

## A plant that must give its findings, and exit nonzero when it has any.
fires : Wanted -> Row
fires = |wanted| {
	plant: "${plants}${wanted.plant}",
	arguments: wanted.arguments,
	counts: wanted.counts,
	says: wanted.says,
	stops: Bool.False,
}

## A plant that must stop, with no summary as its last line.
stops : Wanted -> Row
stops = |wanted| {
	firing = fires(wanted)
	{ ..firing, stops: Bool.True }
}

## A words file below `plants` the plant `clean.roc` is run with, which must
## stop the run saying what it says.
stops_on_words : Str, Str -> Row
stops_on_words = |words_file, said|
	stops({ plant: "clean.roc", arguments: ["words=${plants}${words_file}"], counts: [], says: [said] })

rows : List(Row)
rows = [
	fires({
		plant: "names.roc",
		arguments: [words],
		counts: [("retired_name", 9)],
		says: [
			"names.roc:6: retired_name: tilt_step: tilt is lean here",
			"RideTour: tour is ride here",
			"names.roc:12: retired_name: tour_stops",
			"names.roc:16: retired_name: .tilt",
		],
	}),
	fires({
		plant: "names_unseen.roc",
		arguments: [words],
		counts: [("retired_name", 3)],
		says: [
			"names_unseen.roc:3: retired_name: tilt_now",
			"names_unseen.roc:7: retired_name: tilt_of",
			"names_unseen.roc:12: retired_name: tilt_pair",
		],
	}),
	fires({
		plant: "comments.roc",
		arguments: [words],
		counts: [("retired_comment", 3)],
		says: ["comments.roc:2: retired_comment: tilt is lean here"],
	}),
	fires({ plant: "clean.roc", arguments: [words], counts: [], says: [] }),
	stops({ plant: "broken/*.roc", arguments: [words], counts: [("retired_name", 1)], says: ["refused: "] }),
	stops({ plant: "clean.roc", arguments: [], counts: [], says: ["give words="] }),
	stops_on_words("no_such.txt", "no words file"),
	stops_on_words("words_malformed.txt", "words_malformed.txt:2: expected"),
	stops_on_words("words_twice.txt", "`tilt` is retired twice"),
	stops_on_words("words_both.txt", "`tilt` is both kept and retired"),
]

## The last line of `out` that isn't one of Shale's `──` lines.
last_said : Str -> Str
last_said = |out|
	out.trim().split_on("\n").fold(
		"",
		|last, line|
			if line.starts_with("──") or line.trim() == "" {
				last
			} else {
				line
			},
	)

## The findings `row` wants of `check`.
wanted_count : Row, Str -> U64
wanted_count = |row, check| {
	var $wanted = 0
	for (counted, count) in row.counts {
		if counted == check {
			$wanted = count
		}
	}
	$wanted
}

## What is wrong with one row's run.
find_faults! : Row, { code : I64, out : Str }, List(Str) => List(Str)
find_faults! = |row, result, checks| {
	must_fail = row.stops or !(row.counts.is_empty())
	var $faults = []
	if must_fail and result.code == 0 {
		$faults = $faults.append("it exited 0")
	}
	if !(must_fail) and result.code != 0 {
		$faults = $faults.append("it exited ${result.code.to_str()}")
	}
	for check in checks {
		found = Shale.re_lines!(result.out, "^[^ ]+:[0-9]+: ${check}: ").len()
		wanted = wanted_count(row, check)
		if found != wanted {
			$faults = $faults.append("${check} printed ${found.to_str()}, not ${wanted.to_str()}")
		}
	}
	if !(row.stops) and !(last_said(result.out).contains(" files; ")) {
		$faults = $faults.append("its last line is no summary: ${last_said(result.out)}")
	}
	for literal in row.says {
		if !(result.out.contains(literal)) {
			$faults = $faults.append("its output lacks `${literal}`")
		}
	}
	$faults
}
