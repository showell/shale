## The recipe's self-test: each plant under
## cookbook/roc/plants/find_unclear_names/ run through the launcher, held to
## its findings, its exit and what it says.
import pf.Shale
import lib.Text

SelfTest := [].{

	## Each row run as a child of its own, through `launcher`; a check of
	## `checks` no row fires fails too, since it could be blind.
	run! : List(Str) => {}
	run! = |checks| {
		var $wrong = 0
		var $fired = Set.empty()
		for row in rows {
			argv = List.concat(["shale", launcher, "glob=${row.glob}"], row.args)
			r = Shale.run!({ argv, keep: Whole(4000000), timeout_ms: 120000, want: "" })
			faults = row_faults!(row, r, checks)
			for pair in row.counts {
				$fired = Set.insert($fired, pair.0)
			}
			if List.is_empty(faults) {
				Shale.print!("ok   ${row.glob}")
			} else {
				Shale.print!("FAIL ${row.glob}: ${Str.join_with(faults, "; ")}")
				$wrong = $wrong + 1
				Shale.print!(r.out)
			}
		}
		for check in checks {
			if !(Set.contains($fired, check)) {
				Shale.print!("FAIL `${check}` has no plant")
				$wrong = $wrong + 1
			}
		}
		if $wrong > 0 {
			Shale.fail!("find_unclear_names self_test: ${U64.to_str($wrong)} wrong")
		}
		Shale.print!("find_unclear_names self_test: PASS (${U64.to_str(List.len(rows))} rows: each plant gives its findings and a nonzero exit, each clean case none and exit 0)")
	}
}

## The launcher, which the self-test runs each plant through.
launcher : Str
launcher = "cookbook:roc/find_unclear_names.shale"

Row : { glob : Str, args : List(Str), counts : List((Str, U64)), says : List(Str), stops : Bool }

plants : Str
plants = "cookbook:roc/plants/find_unclear_names/"

clean : Str -> Row
clean = |glob| { glob: "${plants}${glob}", args: [], counts: [], says: [], stops: Bool.False }

fires : Str, List(Str), List((Str, U64)), List(Str) -> Row
fires = |glob, args, counts, says| { glob: "${plants}${glob}", args, counts, says, stops: Bool.False }

stops : Str, List(Str), List(Str) -> Row
stops = |glob, args, says| { glob: "${plants}${glob}", args, counts: [], says, stops: Bool.True }

rows : List(Row)
rows = [
	fires("pure_mutator.roc", [], [("pure_mutator", 7)], ["pure_mutator.roc:3: pure_mutator: `push_cat` is pure and gives a new `List(U64)`", "`get_speed`"]),
	clean("quiet_pure_mutator.roc"),
	fires("terms.roc", [], [("pure_mutator", 2)], ["`set_cookie_header`", "`set_cookies`"]),
	fires("terms.roc", ["terms=${plants}terms.txt"], [("pure_mutator", 1)], ["`set_cookies`"]),
	stops("terms.roc", ["terms=${plants}no_such.txt"], ["no word list"]),
	fires("abbreviation.roc", [], [("abbreviation", 4)], ["abbreviation.roc:3: abbreviation: `max_vis_cats` abbreviates: vis", "`SunPos` abbreviates: pos", "`dist` abbreviates: dist"]),
	clean("quiet_abbreviation.roc"),
	fires("abbreviation.roc", ["abbreviations=${plants}project_words.txt"], [("abbreviation", 4)], ["`max_vis_cats` abbreviates: vis, cats"]),
	fires("short_words/*.roc", [], [("abbreviation", 5)], ["Tbl.roc:1: abbreviation: `Tbl` abbreviates: tbl", "Tbl.roc:4: abbreviation: `Trk` abbreviates: trk", "Tbl.roc:7: abbreviation: `trk` abbreviates: trk", "`pts` abbreviates: pts", "Tbl.roc:9: abbreviation: `hgt` abbreviates: hgt"]),
	clean("quiet_short_words/*.roc"),
	fires("short_words/*.roc", ["accepted=${plants}project_terms.txt"], [("abbreviation", 3)], ["`Tbl` abbreviates: tbl", "`pts` abbreviates: pts", "`hgt` abbreviates: hgt"]),
	{ ..clean("quiet_short_words/*.roc"), args: ["accepted=${plants}project_terms.txt"] },
	stops("quiet_short_words/*.roc", ["accepted=${plants}no_reason.txt"], ["`qty` has no reason"]),
	stops("quiet_abbreviation.roc", ["abbreviations=${plants}no_such.txt"], ["no word list"]),
	fires("long_abbreviations/*.roc", [], [("abbreviation", 11)], ["Parser.roc:5: abbreviation: `attrs` abbreviates: attrs", "`idxes` abbreviates: idxes", "`vals` abbreviates: vals", "`delim` abbreviates: delim", "`parse_args` abbreviates: args", "`params` abbreviates: params", "`elem_count` abbreviates: elem", "`expr_text` abbreviates: expr", "`stmt_kind` abbreviates: stmt", "`tmpl_name` abbreviates: tmpl"]),
	fires("long_abbreviations/*.roc", ["accepted=${plants}project_args.txt"], [("abbreviation", 9)], ["`attrs` abbreviates: attrs", "`params` abbreviates: params"]),
	clean("quiet_long_abbreviations/*.roc"),
	fires("common_words/*.roc", [], [("abbreviation", 2)], ["`Flw` abbreviates: flw", "`night_dmr` abbreviates: dmr"]),
	clean("quiet_common_words/*.roc"),
	fires("tags.roc", [], [("abbreviation", 5)], ["tags.roc:14: abbreviation: `TmplGone` abbreviates: tmpl", "tags.roc:4: abbreviation: `NoRef` abbreviates: ref", "tags.roc:4: abbreviation: `Ref` abbreviates: ref", "tags.roc:6: abbreviation: `TmplMissing` abbreviates: tmpl", "tags.roc:12: abbreviation: `BadAttr` abbreviates: attr"]),
	clean("quiet_tags.roc"),
	fires("module_doc/*.roc", [], [("module_doc", 5)], ["`NoDoc` has no `##` doc", "`Hashless` has no `##` doc", "`Old`'s top comment says `originally`", "`Sky`'s top comment says `used to`", "Sun.roc:3: module_doc: `Sun`'s doc says `originally`"]),
	clean("quiet_module_doc/*.roc"),
	fires("module_doc_private/*.roc", [], [("module_doc", 2)], ["`Shown` has no `##` doc", "`Silent` has no `##` doc"]),
	stops("broken/*.roc", [], ["refused: "]),
]

## The last line of `out` that isn't one of Shale's `──` lines.
last_said : Str -> Str
last_said = |out| List.fold(
	Text.lines(Str.trim(out)),
	"",
	|last, l| if Str.starts_with(l, "──") or Str.trim(l) == "" {
		last
	} else {
		l
	},
)

## What is wrong with one row's run.
row_faults! : Row, { code : I64, out : Str }, List(Str) => List(Str)
row_faults! = |row, r, checks| {
	must_fail = row.stops or !(List.is_empty(row.counts))
	var $faults = []
	if must_fail and r.code == 0 {
		$faults = $faults.append("it exited 0")
	}
	if !(must_fail) and r.code != 0 {
		$faults = $faults.append("it exited ${I64.to_str(r.code)}")
	}
	for check in checks {
		found = List.len(Shale.re_lines!(r.out, "^[^ ]+:[0-9]+: ${check}: "))
		wanted = List.fold(
			row.counts,
			0,
			|n, pair| if pair.0 == check {
				pair.1
			} else {
				n
			},
		)
		if found != wanted {
			$faults = $faults.append("${check} printed ${U64.to_str(found)}, not ${U64.to_str(wanted)}")
		}
	}
	if !(row.stops) and !(Str.contains(last_said(r.out), " files; ")) {
		$faults = $faults.append("its last line is no summary: ${last_said(r.out)}")
	}
	for literal in row.says {
		if !(Str.contains(r.out, literal)) {
			$faults = $faults.append("its output lacks `${literal}`")
		}
	}
	$faults
}
