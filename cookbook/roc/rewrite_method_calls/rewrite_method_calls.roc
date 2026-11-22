## rewrite_method_calls, in Roc on Shale's platform (roc_platform/README.md):
## old-style Roc calls rewritten as method calls, `List.map(xs, f)` as
## `xs.map(f)`, wherever the compiler agrees it is the same call. Its
## launcher, cookbook/roc/rewrite_method_calls.shale, is how it is run, and
## its header says which calls turn, how the types and the compiler judge
## them, and the arguments. check_goldens.shale, beside this, holds its
## output and the files it rewrites on the plants to its goldens.
##
##   cd <repo> && shale cookbook:roc/rewrite_method_calls.shale glob=app/*.roc root=app/main.roc
##   cd <repo> && shale cookbook:roc/rewrite_method_calls.shale glob=app/*.roc root=app/main.roc apply=1
##   shale cookbook:roc/rewrite_method_calls.shale self_test=1
##
## This root reads the arguments, globs the files and prints the verdict.
## Run is the rewrite: the trees (Source), the candidates (Candidates, with
## Literals), the types (Types, judging by Judging and Peers), the trial and
## the confirm (Trial, by Compiler, writing by Rewritten), all on a sheet
## (Tally); Sweep is what the rewrites leave for a sweep; SelfTest runs the
## plants of Plants. Imports, Text and RocTypes are the cookbook's Roc
## library, cookbook/roc/lib/.
## budget: steps=900000000 output_bytes=4000000 arena_bytes=3000000000 kept_bytes=256000000 changes=100000 change_bytes=2000000000 programs=1100 roc_cpu_ms=3600000
app [main!] { lib: "../lib/main.roc", pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale
import Compiler
import Run
import SelfTest
import Sweep
import lib.Text

main! : {} => {}
main! = |{}| {
	self_test = Shale.arg!("self_test", "") == "1"
	apply = self_test or Shale.arg!("apply", "") == "1"
	use_types = either!("types", Shale.arg!("types", "1"))
	count_only = !(either!("trials", Shale.arg!("trials", "1")))
	if count_only and apply and !(self_test) {
		Shale.fail!("rewrite_method_calls: trials=0 writes nothing, so not with apply=1")
	}
	if count_only and !(use_types) {
		Shale.fail!("rewrite_method_calls: trials=0 counts by the types, so not with types=0")
	}
	peers = Shale.arg!("peers", "")
	if peers != "" and peers != "undo" {
		Shale.fail!("rewrite_method_calls: peers=${peers}: peers takes only `undo`")
	}
	undo = peers == "undo"
	if undo and (count_only or !(use_types)) {
		Shale.fail!("rewrite_method_calls: peers=undo judges peers by the types and turns them in trials, so not with trials=0 or types=0 (trials=0 counts them as peer_receiver)")
	}
	plant = Shale.arg!("plant", "")
	if plant != "" and !(self_test) {
		Shale.fail!("rewrite_method_calls: plant= is for the self-test's child runs, and needs self_test=1")
	} else if self_test and plant != "" {
		SelfTest.plant!(plant, Compiler.roc_bin!(Bool.True))
	} else if self_test {
		SelfTest.run!({})
	} else {
		rewrite!({ apply, use_types, count_only, undo })
	}
}

## Whether the argument `name`, given as `value`, is 1 (its default) rather
## than 0; any other value stops the run.
either! : Str, Str => Bool
either! = |name, value| {
	if value != "0" and value != "1" {
		Shale.fail!("rewrite_method_calls: ${name}=${value}: ${name} must be 1 (the default) or 0")
	}
	value == "1"
}

## The rewrite of the glob's files, its summary, its findings and the
## summary again (cookbook/findings.shale), then the files no root reaches.
rewrite! : { apply : Bool, use_types : Bool, count_only : Bool, undo : Bool } => {}
rewrite! = |mode| {
	root = Shale.arg!("root", "")
	from = U64.from_str(Shale.arg!("from", "1")) ?? 1
	types_from = Shale.arg!("types_from", "")
	paths = SelfTest.paths_of_globs!(Shale.arg!("glob", "**/*.roc"))
	roc = Compiler.roc_bin!(Bool.False)
	total = Run.all!({
		paths,
		root,
		from,
		use_types: mode.use_types,
		types_roc: roc,
		judge: { roc, apply: mode.apply },
		count_only: mode.count_only,
		types_from,
		undo: mode.undo,
	})
	made = Sweep.made!(total.tally)
	summary = "${summary_of(mode, total, from, paths.len())}, made_unused_import ${made.unused.to_str()}, made_long_line ${made.long.to_str()}"
	kinds = total.tally.kind_counts
		.sort_with(|(first, _), (second, _)| Text.byte_order(first, second))
		.map(|(kind, count)| "${kind} ${count.to_str()}")
	judged = if mode.use_types {
		["types: the first arguments of the candidates the types judged: ${Str.join_with(kinds, ", ")}"]
	} else {
		[]
	}
	print_findings!(summary, total.lines.concat(made.lines).concat(judged))
	for path in total.unreached {
		Shale.print!("not reached by ${root}: ${path}")
	}
	if !(total.unreached.is_empty()) {
		Shale.fail!("rewrite_method_calls: no root reaches ${total.unreached.len().to_str()} file(s) with a call to try, listed above: their calls were left untried")
	}
}

## The summary line's counts, and where to go on from when the programs ran
## out.
summary_of : { apply : Bool, use_types : Bool, count_only : Bool, undo : Bool }, Run.Total, U64, U64 -> Str
summary_of = |mode, total, from, file_count| {
	count = |n| n.to_str()
	programs = count(total.tally.programs_used)
	counted = if mode.count_only {
		"count: files ${count(total.files)}, candidates ${count(total.candidates)}, proven ${count(total.rewritten)}, unsettled ${count(total.unsettled)}, left ${count(total.left)}, peer_receiver ${count(total.peers)}, programs ${programs} (no trials: the types alone decide)"
	} else {
		undone = if mode.undo {
			"peers=undo, "
		} else {
			""
		}
		dry = if mode.apply {
			""
		} else {
			"dry run: "
		}
		checks = "checks: ${count(total.tally.confirms)} confirming proven calls, ${count(total.tally.rounds)} in trial rounds"
		"${undone}${dry}files ${count(total.files)}, candidates ${count(total.candidates)}, rewritten ${count(total.rewritten)}, left ${count(total.left)}, peer_receiver ${count(total.peers)}, programs ${programs} (${checks})"
	}
	if total.next > 0 {
		"${counted}, in files ${count(from)} to ${count(total.next - 1)} of ${count(file_count)}: a run holds ${count(Run.max_programs)} programs; go on with from=${count(total.next)}"
	} else {
		counted
	}
}

## The summary, each finding, then the summary again (cookbook/findings.shale):
## the first copy survives a cut output, and a gate reads the last line.
print_findings! : Str, List(Str) => {}
print_findings! = |summary, finding_lines| {
	Shale.print!(summary)
	if !(finding_lines.is_empty()) {
		for line in finding_lines {
			Shale.print!(line)
		}
		Shale.print!(summary)
	}
}
