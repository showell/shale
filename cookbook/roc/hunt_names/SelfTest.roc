## The recipe's self-test (the Shale original's naming_plants.shale),
## run after the checks over the plants in cookbook/roc/plants/hunt_names/ and its typed/
## folder: each plant is named for the check it holds, and that check must
## fire on it and on no other check's plant; a `quiet_*.roc` plant must draw
## no finding, and a plant named for no check is a failure; the census must
## give one known word of the plants per verdict its verdict; and two plants'
## findings must say something, or must not.
import pf.Shale
import lib.Finding
import lib.Text
import Passes

SelfTest := [].{

	## What the checks found over the plants: the paths read, the findings
	## reported, the census and the finding lines printed.
	Judged : {
		paths : List(Str),
		findings : List(Finding),
		census : Dict(Str, Passes.Counted),
		finding_lines : List(Str),
	}

	## The plants' folder.
	plants : Str
	plants = "cookbook:roc/plants/hunt_names/"

	## The plants' verdict printed, then a stop when any plant is wrong.
	run! : SelfTest.Judged, List(Str) => {}
	run! = |judged, checks| {
		failures = failures_of(judged, checks)
		for failure in failures {
			Shale.print!("FAIL ${failure}")
		}
		if !(failures.is_empty()) {
			Shale.fail!("hunt_names self_test: ${failures.len().to_str()} plants wrong")
		}
		Shale.print!("hunt_names (Roc) self_test: PASS (each check fires on its plants alone, the quiet_ plants on none, and the census gives each verdict its known word)")
	}
}

## Each way the run over the plants is wrong, as a line.
failures_of : SelfTest.Judged, List(Str) -> List(Str)
failures_of = |judged, checks| {
	fired = Set.from_list(judged.findings.map(|finding| "${finding.path}|${finding.check}"))
	var $failures = []
	for check in checks {
		$planted : U64
		var $planted = 0
		for path in judged.paths {
			is_its_plant = is_plant_for(plant_name(path), check)
			if is_its_plant {
				$planted = $planted + 1
			}
			did_fire = fired.contains("${path}|${check}")
			if is_its_plant and !(did_fire) {
				$failures = $failures.append("`${check}` does not fire on its plant ${path}")
			}
			if did_fire and !(is_its_plant) {
				$failures = $failures.append("`${check}` fires on ${path}, another check's plant")
			}
		}
		if $planted == 0 {
			$failures = $failures.append("`${check}` has no plant")
		}
	}
	# A plant named for no check could never fail.
	for path in judged.paths {
		name = plant_name(path)
		if !(name.starts_with("quiet_") or checks.any(|check| is_plant_for(name, check))) {
			$failures = $failures.append("${path}: a plant the table does not judge")
		}
	}
	for known in census_known {
		match judged.census.get(known.word) {
			Ok(counted) if counted.verdict != known.verdict => {
				$failures = $failures.append("the census calls `${known.word}` ${counted.verdict}, not ${known.verdict}")
			}
			Ok(_) => {}
			Err(_) => {
				$failures = $failures.append("the census has no `${known.word}`")
			}
		}
	}
	for expected in plant_says {
		at = "${SelfTest.plants}${expected.plant}:"
		var $is_said = Bool.False
		for line in judged.finding_lines {
			if line.starts_with(at) and line.contains(expected.says) {
				$is_said = Bool.True
			}
			if line.starts_with(at) and line.contains(expected.never) {
				$failures = $failures.append("${line}: says `${expected.never}`")
			}
		}
		if !($is_said) {
			$failures = $failures.append("${expected.plant}: no finding says `${expected.says}`")
		}
	}
	$failures
}

## The census's verdict for one known word of the plants per verdict.
census_known : List({ word : Str, verdict : Str })
census_known = [
	{ word: "read", verdict: "verb" },
	{ word: "held", verdict: "participle" },
	{ word: "process", verdict: "vague" },
	{ word: "links", verdict: "refused" },
]

## The plants whose finding must say something, or must not: `singular_list`
## offers no plural of an adjective, nor `phrase_name` of a preposition's
## noun.
plant_says : List({ plant : Str, says : Str, never : Str })
plant_says = [
	{ plant: "singular_list_adjective.roc", says: "has no noun", never: "stables" },
	{ plant: "phrase_name_list_holder.roc", says: "starts with a preposition, `by`", never: "by_slots" },
]

is_plant_for : Str, Str -> Bool
is_plant_for = |name, check| name == "${check}.roc" or name.starts_with("${check}_")

## A plant's name below the plants' folder. A typed plant is a CamelCase
## module (`typed/QuestionInferred.roc`), as is a plant another imports
## (`QuietDocShelf.roc`), and its name is read in snake case
## (`question_inferred.roc`).
plant_name : Str -> Str
plant_name = |path| {
	name = path.drop_prefix(SelfTest.plants).drop_prefix("typed/")
	match name.to_utf8().first() {
		Ok(byte) if Text.is_capital(byte) => snake_case(name)
		_ => name
	}
}

## A CamelCase file name in snake case: `_` before each capital but a first,
## and the capital made small.
snake_case : Str -> Str
snake_case = |name| {
	var $out = []
	for byte in name.to_utf8() {
		if Text.is_capital(byte) {
			if !($out.is_empty()) {
				$out = $out.append('_')
			}
			$out = $out.append(byte + 32)
		} else {
			$out = $out.append(byte)
		}
	}
	Str.from_utf8_lossy($out)
}
