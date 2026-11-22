## The rewrite of a list of files, each judged on its root: their trees read,
## their candidates found, the types read of each root and judging them, then
## the confirm and the trials in the files' order, as far as the run's
## programs allow (rewrite_method_calls.shale's `run_all`).
import pf.Shale
import Candidates
import Compiler
import Judging
import Peers
import Source
import Tally
import Trial
import Types
import lib.RocTypes
import lib.Text

Run := [].{

	## What to rewrite: the files, the roots (`a.roc,b.roc`), the file to
	## start at (counted from 1), whether the types are read, the Roc
	## roc-types' pin must match, the Roc that judges, and the modes:
	## `count_only` (`trials=0`), `types_from` and `undo` (`peers=undo`).
	Asked : {
		paths : List(Str),
		root : Str,
		from : U64,
		use_types : Bool,
		types_roc : Str,
		judge : Trial.Judge,
		count_only : Bool,
		types_from : Str,
		undo : Bool,
	}

	## What the run did: its counts, a line for each call that stays (and
	## each peer method call, and under `trials=0` each call the types left
	## unsettled), the file to go on from when the programs ran out (else 0),
	## its sheet, and the files with a call to try that no root reaches.
	Total : {
		files : U64,
		candidates : U64,
		rewritten : U64,
		left : U64,
		unsettled : U64,
		peers : U64,
		lines : List(Str),
		next : U64,
		tally : Tally.Sheet,
		unreached : List(Str),
	}

	## The programs a run may start, Shale's own budget unset.
	max_programs : U64
	max_programs = 1000

	## Rewrites the files of `asked.paths` from the `asked.from`th on.
	all! : Run.Asked => Run.Total
	all! = |asked| {
		if asked.use_types and asked.root == "" {
			Shale.fail!("rewrite_method_calls: the types need root=, an app importing every file of the glob: roc-types reads an app, and a type module only through one. Give root=, or types=0 to run without them, every candidate tried")
		}
		if !(asked.use_types) and asked.root.contains(",") {
			Shale.fail!("rewrite_method_calls: root=${asked.root}: several roots need the types, which say which root reaches a file; give one root, or run with the types")
		}
		roots = if asked.root == "" {
			[]
		} else {
			match RocTypes.root_apps("rewrite_method_calls", asked.root) {
				Ok(apps) => apps
				Err(why) => {
					Shale.fail!(why)
					[]
				}
			}
		}
		if asked.types_from != "" and roots.len() != 1 {
			Shale.fail!("rewrite_method_calls: types_from= holds one root's types, and root= names ${roots.len().to_str()}")
		}
		files = parsed!(asked.paths)
		aliases = if asked.use_types {
			Peers.declared_aliases(files.values())
		} else {
			Dict.empty()
		}
		symmetric = match Peers.symmetric_table(Compiler.read_text!(symmetric_functions)) {
			Ok(names) => names
			Err(why) => {
				Shale.fail!(why)
				Set.empty()
			}
		}
		var $judged = []
		for path in asked.paths.drop_first(asked.from - 1) {
			file = match files.get(path) {
				Ok(found) => found
				Err(_) => {
					Shale.fail!("rewrite_method_calls: roc-ast gave no tree for ${path}")
					Source.of(path, "", [])
				}
			}
			cands = Candidates.of_file(file, module_of_path(path))
			method_calls = if asked.use_types {
				Peers.method_calls(file, symmetric)
			} else {
				[]
			}
			open = cands.any(|cand| cand.why_left == "") or !(method_calls.is_empty())
			$judged = $judged.append({ path, file, real: Shale.real!(path), root: roots.first() ?? path, cands, counts: [], open, unreached: Bool.False, method_calls, peers: [] })
		}
		# The types judge only open candidates, so with none, roc-types never runs.
		read_types = asked.use_types and $judged.any(|one| one.open)
		typing = { by_region: Dict.empty(), aliases, symmetric }
		typed = if read_types {
			with_types!(asked, roots, typing, $judged)
		} else {
			{ tally: Tally.ran(Tally.empty), judged: $judged }
		}
		unreached = typed.judged.keep_if(|one| one.unreached).map(|one| one.path)
		in_mode = typed.judged.map(|one| as_undo(asked.undo, one))
		ready = in_mode.map(with_open_count)
		total = {
			files: 0,
			candidates: 0,
			rewritten: 0,
			left: 0,
			unsettled: 0,
			peers: 0,
			lines: [],
			next: 0,
			tally: typed.tally,
			unreached,
		}
		if asked.count_only {
			counted_by_types(total, ready)
		} else {
			tried!(asked, total, ready)
		}
	}
}

## The symmetric functions' list.
symmetric_functions : Str
symmetric_functions = "cookbook:roc/symmetric_functions.txt"

## A file as the run judges it: its tree, real path and root, its
## candidates, the kinds of type the types found, whether the types must
## judge it (`open`: a module call to try, or with the types a symmetric
## method call), whether no root reaches it, its symmetric method calls and
## its peer method calls; then how many calls are open and whether all are
## proven.
Judged : {
	path : Str,
	file : Source.File,
	real : Str,
	root : Str,
	cands : List(Candidates.Candidate),
	counts : Judging.KindCounts,
	open : Bool,
	unreached : Bool,
	method_calls : List(Peers.MethodCall),
	peers : List(Candidates.Candidate),
}

Ready : {
	path : Str,
	file : Source.File,
	root : Str,
	cands : List(Candidates.Candidate),
	counts : Judging.KindCounts,
	unreached : Bool,
	peers : List(Candidates.Candidate),
	open_count : U64,
	all_proven : Bool,
}

## The module a .roc file holds, its name without `.roc`.
module_of_path : Str -> Str
module_of_path = |path| Text.basename(path).drop_suffix(".roc")

## Every file's tree from Roc's parser and its text, by path: one program.
parsed! : List(Str) => Dict(Str, Source.File)
parsed! = |paths| {
	var $files = Dict.empty()
	if !(paths.is_empty()) {
		for tree in Shale.roc_tree!(paths) {
			if tree.err != "" {
				Shale.fail!("rewrite_method_calls: roc-ast: ${tree.path}: ${tree.err}")
			}
			$files = $files.insert(tree.path, Source.of(tree.path, Compiler.read_text!(tree.path), tree.nodes))
		}
	}
	$files
}

## The types read of each root, one at a time, each file's root the first
## whose app reaches it, its open candidates judged by that root's types on
## the way. A file with a call to try that no root reaches is `unreached`,
## and the run goes on without it.
with_types! : Run.Asked, List(Str), Judging.Typing, List(Judged) => { tally : Tally.Sheet, judged : List(Judged) }
with_types! = |asked, roots, typing, judged| {
	only = if asked.types_from == "" {
		Set.empty()
	} else {
		var $reals = Set.empty()
		for path in asked.paths {
			$reals = $reals.insert(Shale.real!(path))
		}
		$reals
	}
	var $tally = Tally.ran(Tally.empty)
	var $judged = judged
	var $found = Set.empty()
	for root in roots {
		if asked.types_from == "" {
			$tally = Tally.ran($tally)
		}
		typed = Types.of_root!({ root, roc: asked.types_roc, types_from: asked.types_from, only })
		if !($tally.typed_roots.contains(root)) {
			$tally = { ..$tally, typed_roots: $tally.typed_roots.append(root) }
		}
		types_here = { ..typing, by_region: typed.by_region }
		var $judged_here = []
		for one in $judged {
			if one.open and !($found.contains(one.path)) and typed.modules.contains(one.real) {
				$found = $found.insert(one.path)
				by_types = Judging.by_types(one.counts, one.file, one.cands, one.real, types_here)
				peers = one.peers.concat(Judging.method_peers(one.file, one.method_calls, one.real, types_here))
				$judged_here = $judged_here.append({ ..one, root, counts: by_types.counts, cands: by_types.cands, peers })
			} else {
				$judged_here = $judged_here.append(one)
			}
		}
		$judged = $judged_here
	}
	var $marked = []
	for one in $judged {
		$marked = $marked.append({ ..one, unreached: one.open and !($found.contains(one.path)) })
	}
	{ tally: $tally, judged: $marked }
}

## Under `peers=undo`, a file's candidates are its peer method calls alone.
as_undo : Bool, Judged -> Judged
as_undo = |undo, one|
	if undo {
		{ ..one, cands: one.peers, peers: [], counts: [] }
	} else {
		one
	}

with_open_count : Judged -> Ready
with_open_count = |one| {
	open_cands = one.cands.keep_if(|cand| cand.why_left == "")
	all_proven = !(open_cands.is_empty()) and !(one.unreached) and open_cands.all(|cand| cand.proven)
	{
		path: one.path,
		file: one.file,
		root: one.root,
		cands: one.cands,
		counts: one.counts,
		unreached: one.unreached,
		peers: one.peers,
		open_count: open_cands.len(),
		all_proven,
	}
}

## The file's peer method calls added to the findings, a `path:line:
## peer_receiver: why` each, and to their count.
with_peer_findings : Run.Total, Ready -> Run.Total
with_peer_findings = |total, one| {
	said = one.peers.map(
		|cand| {
			why = "`.${cand.fn}` is symmetric and its receiver and first argument are both ${cand.module_name}s, so neither is the subject"
			"${one.path}:${cand.line.to_str()}: peer_receiver: ${why}: ${cand.module_name}.${cand.fn}(...)"
		},
	)
	{ ..total, peers: total.peers + one.peers.len(), lines: total.lines.concat(said) }
}

## A file's kinds of type added to the sheet's, once the file is begun.
with_counts : Run.Total, Ready -> Run.Total
with_counts = |total, one| {
	counts = one.counts.fold(total.tally.kind_counts, |all, (kind, count)| Judging.counted(all, kind, count))
	{ ..total, tally: { ..total.tally, kind_counts: counts } }
}

## A call that stays, as its finding line.
left_line : Str, Candidates.Candidate -> Str
left_line = |path, cand| "${path}:${cand.line.to_str()}: ${cand.module_name}.${cand.fn}: ${cand.why_left}"

## `trials=0`'s answer: a proven call counts as rewritten, an open one the
## types didn't settle as unsettled, with a line naming the kind of type
## they gave, and a call that stays as left, with its reason. Nothing is
## written or compiled.
counted_by_types : Run.Total, List(Ready) -> Run.Total
counted_by_types = |total, ready| {
	var $total = total
	for one in ready {
		cands = if one.unreached {
			[]
		} else {
			one.cands
		}
		proven = cands.keep_if(|cand| cand.why_left == "" and cand.proven)
		if !(proven.is_empty()) {
			$total = { ..$total, tally: Tally.noted($total.tally, one.file, proven) }
		}
		if !(one.unreached) {
			$total = { ..$total, files: $total.files + 1 }
		}
		$total = with_counts($total, one)
		$total = { ..$total, candidates: $total.candidates + cands.len() }
		$total = with_peer_findings($total, one)
		for cand in cands {
			if cand.why_left != "" {
				$total = { ..$total, left: $total.left + 1, lines: $total.lines.append(left_line(one.path, cand)) }
			} else if cand.proven {
				$total = { ..$total, rewritten: $total.rewritten + 1 }
			} else {
				unsettled = "${one.path}:${cand.line.to_str()}: ${cand.module_name}.${cand.fn}: unsettled: the types gave ${cand.kind}, and only a trial would tell"
				$total = { ..$total, unsettled: $total.unsettled + 1, lines: $total.lines.append(unsettled) }
			}
		}
	}
	$total
}

## The `roc test` summary of `root` before any rewrite, which every trial on
## that root must match: taken once a root, and `roc check` must pass first.
baseline! : Trial.Judge, Tally.Sheet, Dict(Str, Str), Str => { tally : Tally.Sheet, baselines : Dict(Str, Str), summary : Str }
baseline! = |judge, tally, baselines, root|
	match baselines.get(root) {
		Ok(summary) => { tally, baselines, summary }
		Err(_) => {
			if !(Compiler.check!(judge.roc, root).ok) {
				Shale.fail!("rewrite_method_calls: `roc check ${root}` fails before any rewrite: fix it first")
			}
			summary = Compiler.test_summary!(judge.roc, root)
			{ tally: Tally.ran(Tally.ran(tally)), baselines: baselines.insert(root, summary), summary }
		}
	}

## The confirm and the trials, file by file. A file is begun only while the
## programs it may take are left: `Trial.max_rounds` checks, a test and,
## under `apply`, two `roc fmt`s, and the first file on a root also the
## baseline's check and test.
tried! : Run.Asked, Run.Total, List(Ready) => Run.Total
tried! = |asked, total, ready| {
	judge = asked.judge
	per_file = Trial.max_rounds + 5
	var $total = total
	var $baselines = Dict.empty()
	var $place = 0
	for one in ready {
		$place = $place + 1
		cands = if one.unreached {
			[]
		} else {
			one.cands
		}
		if !(one.unreached) and one.all_proven and !($total.tally.confirmed.contains(one.path)) {
			# The programs taken before this file's: the baseline's two, if its
			# root has none yet, the confirm's checks and test, and this file's
			# own trial, should the confirm set it aside. Under `apply` each file
			# in the confirm also takes two `roc fmt`s, so only as many join as
			# fit.
			baseline_programs = if $baselines.contains(one.root) {
				0
			} else {
				2
			}
			taken = $total.tally.programs_used + baseline_programs + Trial.max_rounds + 1 + per_file
			var $files = []
			for other in ready.drop_first($place - 1) {
				fits = !(judge.apply) or 2 * ($files.len() + 1) + taken <= Run.max_programs
				if other.all_proven and other.root == one.root and !($total.tally.confirmed.contains(other.path)) and fits {
					$files = $files.append({ path: other.path, file: other.file, cands: other.cands })
				}
			}
			if taken > Run.max_programs or $files.is_empty() {
				return { ..$total, next: asked.from + $place - 1 }
			}
			based = baseline!(judge, $total.tally, $baselines, one.root)
			$baselines = based.baselines
			$total = { ..$total, tally: Trial.confirm!(judge, based.tally, one.root, $files, based.summary) }
		}
		kept_by_confirm = $total.tally.confirmed.get(one.path) ?? 0
		if !(one.unreached) and one.open_count > 0 and kept_by_confirm == 0 and $total.tally.programs_used + per_file > Run.max_programs {
			return { ..$total, next: asked.from + $place - 1 }
		}
		if !(one.unreached) {
			$total = { ..$total, files: $total.files + 1 }
		}
		$total = with_counts($total, one)
		$total = with_peer_findings($total, one)
		if !(cands.is_empty()) {
			var $cands = Candidates.without_lines(cands)
			$kept : U64
			var $kept = kept_by_confirm
			if $kept == 0 and one.open_count > 0 {
				based = baseline!(judge, $total.tally, $baselines, one.root)
				$baselines = based.baselines
				done = Trial.try_file!(judge, based.tally, one.file, $cands, one.root, based.summary)
				$total = { ..$total, tally: done.tally }
				$cands = done.cands
				$kept = done.turned
			}
			left = $cands.keep_if(|cand| cand.why_left != "")
			$total = {
				..$total,
				candidates: $total.candidates + $cands.len(),
				rewritten: $total.rewritten + $kept,
				left: $total.left + left.len(),
				lines: $total.lines.concat(left.map(|cand| left_line(one.path, cand))),
			}
		}
	}
	$total
}
