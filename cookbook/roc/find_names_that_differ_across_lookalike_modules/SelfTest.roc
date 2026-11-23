## The recipe's self-test: each plant under
## cookbook/roc/plants/find_names_that_differ_across_lookalike_modules/ run through the launcher, held to its
## findings, its exit and what it says.
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
			Shale.fail!("find_names_that_differ_across_lookalike_modules self_test: ${$wrong.to_str()} wrong")
		}
		row_count = rows.len().to_str()
		proof = "each plant gives its findings and a nonzero exit, each clean case none and exit 0"
		Shale.print!("find_names_that_differ_across_lookalike_modules self_test: PASS (${row_count} rows: ${proof})")
	}
}

## The launcher, which the self-test runs each plant through.
launcher : Str
launcher = "cookbook:roc/find_names_that_differ_across_lookalike_modules.shale"

plants : Str
plants = "cookbook:roc/plants/find_names_that_differ_across_lookalike_modules/"

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

## A plant, given as a glob below `plants`, with no finding to give.
quiet : Str, List(Str) -> Row
quiet = |plant, arguments| {
	plant: "${plants}${plant}",
	arguments,
	counts: [],
	says: [],
	stops: Bool.False,
}

## What a plant that fires is run with and must give.
Firing : {
	plant : Str,
	arguments : List(Str),
	counts : List((Str, U64)),
	says : List(Str),
}

## What a plant that stops is run with and must say.
Stopping : {
	plant : Str,
	arguments : List(Str),
	says : List(Str),
}

## A plant with findings, and what they say.
fires : Firing -> Row
fires = |wanted| {
	plant: "${plants}${wanted.plant}",
	arguments: wanted.arguments,
	counts: wanted.counts,
	says: wanted.says,
	stops: Bool.False,
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

rows : List(Row)
rows = [
	fires({
		plant: "family/*.roc",
		arguments: [],
		counts: [("family", 1)],
		says: [
			"RailSighting.roc:4: family: `RailSighting` has no `in_view`, "
				.concat("which 3 of the 4 `*sighting` modules have"),
		],
	}),
	quiet("family_answered/*.roc", []),
	quiet("family_small/*.roc", []),
	quiet("family_half/*.roc", []),
	fires({
		plant: "twin_name/*.roc",
		arguments: [],
		counts: [("twin_name", 1)],
		says: ["Bird.roc:4: twin_name: `Pose` is also declared at", "Cat.roc:4"],
	}),
	quiet("twin_name/*.roc", ["roles=${plants}roles.txt"]),
	fires({
		plant: "twin_name/*.roc",
		arguments: ["roles=${plants}other_roles.txt"],
		counts: [("twin_name", 1)],
		says: ["`Pose` is also declared at"],
	}),
	stops({
		plant: "twin_name/*.roc",
		arguments: ["roles=${plants}no_such.txt"],
		says: ["no word list"],
	}),
	quiet("twin_app/*.roc", []),
	quiet("twin_copy/*.roc", []),
	quiet("twin_namespace/*.roc", []),
	fires({
		plant: "twin_namespace_clash/*.roc",
		arguments: [],
		counts: [("twin_name", 1)],
		says: ["`Lamp` is also declared at"],
	}),
	fires({
		plant: "conversion/*.roc",
		arguments: [],
		counts: [("unpaired_conversion", 1)],
		says: ["Span.roc:4: unpaired_conversion: `Span.metres` gives its own type beside `Span.to_metres`"],
	}),
	quiet("conversion_paired/*.roc", []),
	stops({ plant: "broken/*.roc", arguments: [], says: ["refused: "] }),
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
