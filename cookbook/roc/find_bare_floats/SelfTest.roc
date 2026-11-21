## The recipe's self-test: each plant under
## cookbook/roc/plants/find_bare_floats/ run through the launcher, held to its
## findings, its exit and what it says (cookbook/roc/gate_recipe.shale's
## `check_plants`). It copies cookbook/roc/check_families/SelfTest.roc's
## runner, on purpose, until the Roc recipes share a library.
import pf.Shale

SelfTest := [].{

	## Each row run as a child of its own, through `launcher`; a check of
	## `checks` no row fires fails too, since it could be blind.
	run! : List(Str) => {}
	run! = |checks| {
		$wrong : U64
		var $wrong = 0
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
			Shale.fail!("find_bare_floats self_test: ${$wrong.to_str()} wrong")
		}
		row_count = rows.len().to_str()
		proof = "each plant gives its findings and a nonzero exit, each clean case none and exit 0"
		Shale.print!("find_bare_floats self_test: PASS (${row_count} rows: ${proof})")
	}
}

## The launcher, which the self-test runs each plant through.
launcher : Str
launcher = "cookbook:roc/find_bare_floats.shale"

plants : Str
plants = "cookbook:roc/plants/find_bare_floats/"

## A plant's run: its files (a glob below `plants`) and its other arguments,
## the findings each check must print, the literals its output must hold,
## and whether it must stop without a finding.
Row : {
	plant : Str,
	arguments : List(Str),
	counts : List((Str, U64)),
	says : List(Str),
	stops : Bool,
}

## A plant's run, before its place below `plants` is spelled out.
Wanted : {
	plant : Str,
	arguments : List(Str),
	counts : List((Str, U64)),
	says : List(Str),
}

## A plant that must give its findings, or none when `counts` is empty.
gives : Wanted -> Row
gives = |wanted| {
	plant: "${plants}${wanted.plant}",
	arguments: wanted.arguments,
	counts: wanted.counts,
	says: wanted.says,
	stops: Bool.False,
}

## What a plant that stops is run with and must say.
Stopping : {
	plant : Str,
	arguments : List(Str),
	says : List(Str),
}

## A plant that must stop before any finding, saying what it says.
stops : Stopping -> Row
stops = |wanted| {
	plant: "${plants}${wanted.plant}",
	arguments: wanted.arguments,
	counts: [],
	says: wanted.says,
	stops: Bool.True,
}

## The table argument for a table below `plants`.
table : Str -> Str
table = |name| "table=${plants}${name}"

rows : List(Row)
rows = [
	gives({
		plant: "Signatures.roc",
		arguments: [],
		counts: [("bare_float", 12)],
		says: [
			"Signatures.roc:5: bare_float: `Signatures.Reading` has a bare `F64`: F64",
			"`Signatures.scaled` has a bare `F64`: (Reading, F64 -> Reading)",
			"`Signatures.Trip.length` has a bare `F64`: { length : F64, name : Str }",
			"`Signatures.pair` has a bare `F64`: (F64, F64)",
		],
	}),
	gives({ plant: "Quiet.roc", arguments: [], counts: [], says: [] }),
	gives({
		plant: "Helpers.roc",
		arguments: [],
		counts: [("bare_float", 7)],
		says: [
			"`Helpers.used_by_a_member` has",
			"`Helpers.used_by_a_definition` has",
			"`Helpers.reached_from_production` has",
			"`Helpers.in_a_short_record` has",
			"`Helpers.used_by_nothing` has",
			"`Helpers.only_itself` has",
			"`Helpers.member_tests_use` has",
		],
	}),
	gives({
		plant: "QuietHelpers.roc",
		arguments: [],
		counts: [],
		says: ["3 bare `F64`s are in helpers only tests use"],
	}),
	gives({
		plant: "helpers_app.roc",
		arguments: [],
		counts: [("bare_float", 1)],
		says: ["`helpers_app.only_tests` has"],
	}),
	gives({
		plant: "whole/*.roc",
		arguments: [],
		counts: [("bare_float", 7)],
		says: [],
	}),
	gives({
		plant: "whole/*.roc",
		arguments: [table("whole/table.txt")],
		counts: [],
		says: [],
	}),
	gives({
		plant: "whole/*.roc",
		arguments: [table("whole/narrow.txt")],
		counts: [("bare_float", 2)],
		says: ["`Held.Spot.left_out` has a bare", "`Held.other` has a bare"],
	}),
	gives({
		plant: "whole/*.roc",
		arguments: [table("whole/shadowed.txt")],
		counts: [("unused_exception", 1)],
		says: ["`Held` excuses nothing"],
	}),
	gives({
		plant: "whole/*.roc",
		arguments: [table("whole/unused.txt")],
		counts: [("unused_exception", 4)],
		says: [
			"unused.txt:4: unused_exception: `Held.gone` excuses nothing",
			"`Unit.plus` excuses nothing",
			"`Held.Spot.gone` excuses nothing",
			"`Vanished` names a module no file here holds",
		],
	}),
	gives({
		plant: "whole/Held.roc",
		arguments: [table("whole/table.txt")],
		counts: [],
		says: [],
	}),
	gives({
		plant: "whole/Unit.roc",
		arguments: [table("whole/unused.txt")],
		counts: [("unused_exception", 2)],
		says: ["`Unit.plus` excuses nothing", "`Vanished` names a module"],
	}),
	stops({ plant: "broken/*.roc", arguments: [], says: ["refused: "] }),
	stops({
		plant: "Quiet.roc",
		arguments: [table("no_such.txt")],
		says: ["no table"],
	}),
	stops({
		plant: "Quiet.roc",
		arguments: [table("table_no_reason.txt")],
		says: ["table_no_reason.txt:2: expected"],
	}),
	stops({
		plant: "Quiet.roc",
		arguments: [table("table_twice.txt")],
		says: ["`Quiet.name` is listed twice"],
	}),
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
	$wanted : U64
	var $wanted = 0
	for (counted, count) in row.counts {
		if counted == check {
			$wanted = count
		}
	}
	$wanted
}

## What is wrong with one row's run (cookbook/roc/gate_recipe.shale's
## `row_faults`).
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
