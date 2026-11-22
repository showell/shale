## The recipe's self-test: each plant under
## cookbook/roc/plants/check_name_types/ run through the launcher, held to its
## findings, its exit and what it says. It copies
## cookbook/roc/check_families/SelfTest.roc but for its rows.
import pf.Shale

SelfTest := [].{

	## Each row run as a child of its own, through `launcher`; a check of
	## `checks` no row fires fails too, since it could be blind.
	run! : List(Str) => {}
	run! = |checks| {
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
			Shale.fail!("check_name_types self_test: ${$wrong.to_str()} wrong")
		}
		row_count = rows.len().to_str()
		proof = "each plant gives its findings and a nonzero exit, each clean case none and exit 0"
		Shale.print!("check_name_types self_test: PASS (${row_count} rows: ${proof})")
	}
}

## The launcher, which the self-test runs each plant through.
launcher : Str
launcher = "cookbook:roc/check_name_types.shale"

plants : Str
plants = "cookbook:roc/plants/check_name_types/"

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

## The argument that names one of the plants' tables.
table : Str -> Str
table = |file| "table=${plants}${file}"

rows : List(Row)
rows = [
	fires({
		plant: "name_type.roc",
		arguments: [table("table.txt")],
		counts: [("name_type", 6)],
		says: [
			"name_type.roc:3: name_type: `bull_dist` is `Geom.Along`, "
				.concat("and a name ending `dist` is `Distance` here"),
			"`in_2_frames` ends in `frames`",
		],
	}),
	quiet("name_type.roc", []),
	quiet("quiet_name_type.roc", [table("table.txt")]),
	fires({
		plant: "bare_beside_unit/*.roc",
		arguments: [],
		counts: [("bare_beside_unit", 2)],
		says: [
			"Rider.roc:4: bare_beside_unit: the field `forward` is a bare `F64` here "
				.concat("and a `Distance` at"),
		],
	}),
	quiet("quiet_bare/*.roc", []),
	fires({
		plant: "bare_any/*.roc",
		arguments: [],
		counts: [("bare_beside_unit", 1)],
		says: ["the field `id` is a bare `U64` here"],
	}),
	quiet("bare_any/*.roc", [table("table_any.txt")]),
	quiet("name_type.roc", [table("table_any.txt")]),
	fires({
		plant: "no_value.roc",
		arguments: [],
		counts: [("no_value", 4)],
		says: ["no_value.roc:9: no_value: `no_frames` means none and is a `Steps`"],
	}),
	quiet("quiet_no_value.roc", []),
	stops({ plant: "broken/*.roc", arguments: [], says: ["refused: "] }),
	stops({
		plant: "quiet_name_type.roc",
		arguments: [table("no_such.txt")],
		says: ["no table"],
	}),
	stops({
		plant: "quiet_name_type.roc",
		arguments: [table("table_malformed.txt")],
		says: ["table_malformed.txt:2: expected"],
	}),
	stops({
		plant: "quiet_name_type.roc",
		arguments: [table("table_twice.txt")],
		says: ["`dist` is listed twice"],
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
