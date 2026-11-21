## The recipe's self-test: each plant under
## cookbook/roc/plants/find_unused_imports/ run through the launcher, held to
## its findings, its exit and what it says (cookbook/roc/gate_recipe.shale's
## `check_plants`).
import pf.Shale

SelfTest := [].{

	## Each row run as a child of its own, through the launcher; a run that
	## fires no `unused_import` anywhere fails too, since the check could be
	## blind.
	run! : {} => {}
	run! = |{}| {
		var $wrong = 0
		for row in rows {
			pattern = "${plants}${row.plant}"
			command = ["shale", launcher, "glob=${pattern}"]
			result = Shale.run!({
				argv: command,
				keep: Whole(4000000),
				timeout_ms: 120000,
				want: "",
			})
			faults = find_faults!(row, result)
			if faults.is_empty() {
				Shale.print!("ok   ${pattern}")
			} else {
				Shale.print!("FAIL ${pattern}: ${Str.join_with(faults, "; ")}")
				$wrong = $wrong + 1
				Shale.print!(result.out)
			}
		}
		if !(rows.any(|row| row.findings > 0)) {
			Shale.print!("FAIL `unused_import` has no plant")
			$wrong = $wrong + 1
		}
		if $wrong > 0 {
			Shale.fail!("find_unused_imports self_test: ${$wrong.to_str()} wrong")
		}
		row_count = rows.len().to_str()
		proof = "each plant gives its findings and a nonzero exit, each clean case none and exit 0"
		Shale.print!("find_unused_imports self_test: PASS (${row_count} rows: ${proof})")
	}
}

## The launcher, which the self-test runs each plant through.
launcher : Str
launcher = "cookbook:roc/find_unused_imports.shale"

plants : Str
plants = "cookbook:roc/plants/find_unused_imports/"

## A plant's run: its files below `plants`, the findings it must print, the
## literals its output must hold, and whether it must stop without a finding.
Row : {
	plant : Str,
	findings : U64,
	says : List(Str),
	stops : Bool,
}

## A plant with no finding to give.
quiet : Str -> Row
quiet = |plant| {
	plant,
	findings: 0,
	says: [],
	stops: Bool.False,
}

## A plant with one finding, and what it says.
fires_once : Str, Str -> Row
fires_once = |plant, saying| {
	plant,
	findings: 1,
	says: [saying],
	stops: Bool.False,
}

rows : List(Row)
rows = [
	fires_once("unused.roc", "unused.roc:3: unused_import: `game.Polygon` is imported"),
	fires_once("unused_alias.roc", "`game.Distance` as `D` is imported and never used"),
	fires_once("unused_exposing.roc", "`pf.Stdout` is imported"),
	fires_once("unused_file_import.roc", "the file import `page` is imported"),
	fires_once("unused_in_prose.roc", "`game.Polygon` is imported"),
	fires_once("unused_method_only.roc", "`game.Distance` is imported"),
	fires_once("unused_tag_namesake.roc", "`game.Other` is imported"),
	fires_once("unused_beside_hosted.roc", "unused_beside_hosted.roc:24: unused_import: `Http`"),
	{
		plant: "unused.roc,${plants}unused_alias.roc",
		findings: 2,
		says: [],
		stops: Bool.False,
	},
	quiet("quiet_qualified.roc"),
	quiet("quiet_annotation.roc"),
	quiet("quiet_expect.roc"),
	quiet("quiet_pattern_tag.roc"),
	quiet("quiet_tag.roc"),
	quiet("quiet_exposing.roc"),
	quiet("quiet_alias.roc"),
	quiet("quiet_interpolation.roc"),
	quiet("quiet_file_import.roc"),
	quiet("quiet_hosted.roc"),
	{
		plant: "broken/*.roc",
		findings: 0,
		says: ["refused: "],
		stops: Bool.True,
	},
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

## What is wrong with one row's run (cookbook/roc/gate_recipe.shale's
## `row_faults`).
find_faults! : Row, { code : I64, out : Str } => List(Str)
find_faults! = |row, result| {
	must_fail = row.stops or row.findings > 0
	var $faults = []
	if must_fail and result.code == 0 {
		$faults = $faults.append("it exited 0")
	}
	if !(must_fail) and result.code != 0 {
		$faults = $faults.append("it exited ${result.code.to_str()}")
	}
	found = Shale.re_lines!(result.out, "^[^ ]+:[0-9]+: unused_import: ").len()
	if found != row.findings {
		printed = "unused_import printed ${found.to_str()}"
		$faults = $faults.append("${printed}, not ${row.findings.to_str()}")
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
