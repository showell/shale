## What each plant in cookbook/roc/plants/rewrite_method_calls/ must come to
## (rewrite_method_calls.shale's `EXPECT`), and the plants that serve only
## as roots for others.
Plants := [].{

	## A plant's expectation: how many calls it must leave (`left`), a
	## reason one of them must give (`why`), the root it is judged on
	## (`root`, the plant itself when `""`), whether it runs with the types,
	## and how many confirms and trial rounds it takes. A plant with `file`
	## judges that plant's file under its own name, one with `with` reads a
	## second plant beside it, as a glob of two files would, and one with
	## `stop` must stop its run with that text. A `count` plant runs
	## `trials=0` and must count `proven` and `unsettled` calls.
	Expect : {
		name : Str,
		file : Str,
		beside : Str,
		left : U64,
		why : Str,
		root : Str,
		types : Bool,
		confirms : U64,
		rounds : [Rounds(U64), AnyRounds],
		no_types_run : Bool,
		unreached : U64,
		stop : Str,
		tree_pin : Str,
		count : Bool,
		proven : U64,
		unsettled : U64,
		types_from : Bool,
		peers : [Peers(U64), AnyPeers],
		undo : Bool,
		made_unused : U64,
		made_long : U64,
		made_says : Str,
	}

	## Every plant's expectation, in the table's order.
	table : List(Plants.Expect)
	table = [
		{ ..plain("Rewrite"), left: 0 },
		{ ..plain("Chained"), left: 0 },
		{ ..plain("FunctionValue"), left: 0 },
		{ ..plain("Pipe"), left: 1, why: "the right of a `|>`" },
		{ ..plain("Own"), left: 0 },
		{ ..plain("OtherType"), left: 1, why: "roc check: missing method" },
		{ ..plain("Decode"), left: 1, why: "on no rewritten call; the file is put back", root: "Breaks" },
		{ ..plain("Breaks"), left: 1, why: "roc check: missing method" },
		{ ..plain("Matched"), left: 1, why: "an argument with no span" },
		{ ..plain("Literal"), left: 8, why: "a number literal's type would default as a receiver" },
		{ ..plain("Ref"), left: 0 },
		{ ..plain("Scrutinee"), left: 1, why: "roc check: missing method (on `text`'s definition)" },
		{ ..plain("Knock"), left: 2, why: "roc check: missing method" },
		{ ..plain("Header"), left: 0 },
		{ ..typed("Typed", "TypedApp", 1, 0), left: 1, why: "the first argument is a List, not a Str" },
		{ ..typed("Proven", "TypedApp", 1, 0), left: 0 },
		{ ..typed("NoCalls", "TypedApp", 0, 0), left: 0, no_types_run: Bool.True },
		{ ..typed("ProvenFails", "TypedApp", 1, 2), left: 2, why: "roc check: type mismatch" },
		{ ..plain("Unrooted"), types: Bool.True, stop: "the types need root=" },
		{ ..plain("NotApp"), types: Bool.True, root: "Proven", stop: "is not an app" },
		{ ..plain("Pin"), types: Bool.True, root: "TypedApp", stop: "roc-types reads Roc nightly-2026-11-03-34bd006", tree_pin: "nightly-2026-10-29-a0a9fc2" },
		{ ..typed("Second", "TypedApp,SecondApp", 1, 0), left: 0 },
		# A file no root reaches is listed, and the run goes on to the next.
		{ ..typed("Unreached", "TypedApp,SecondApp", 1, 0), file: "Proven", beside: "Unreached", left: 0, unreached: 1 },
		{ ..typed("Beside", "TypedApp", 1, 0), file: "Proven", beside: "NoCalls", left: 0 },
		# A qualified type whose last segment names the module proves nothing,
		# and the compiler agrees: the trial refuses both calls.
		{ ..typed("LastSegment", "TypedApp", 0, 0), left: 2, why: "the first argument is a Shelf.Book, not a Book" },
		{ ..plain("LastSegmentTried"), file: "LastSegment", left: 2, why: "roc check: missing method" },
		# A package's type module, imported as `pk.Meter`, proves its calls.
		{ ..typed("Packaged", "PkgApp", 1, 0), left: 0 },
		# `trials=0`: the types alone count, and the plant comes out as it was.
		# ProvenFails' three calls are all proven, where the trial leaves two:
		# the count's cost, held here.
		{ ..counted("TypedCount", "Typed", 1), left: 1, why: "the first argument is a List, not a Str" },
		{ ..counted("ProvenFailsCount", "ProvenFails", 3), left: 0 },
		{ ..counted("ProvenFrom", "Proven", 2), types_from: Bool.True, left: 0 },
		{ ..counted("LastSegmentCount", "LastSegment", 0), left: 2, why: "the first argument is a Shelf.Shelf, not a Shelf" },
		{ ..counted("PackagedCount", "Packaged", 1), root: "PkgApp", left: 0 },
		# A symmetric function's peer calls stay module calls; `peers=undo`
		# turns its peer method calls back; every other function's calls turn
		# or stay as ever. Each `count` plant's `left` counts its listed lines,
		# the peer_receiver findings among them. Line 24 of each is the alias's.
		{ ..typed("Peers", "TypedApp", 1, 0), left: 6, why: "Peers.roc:24: U64.max: peers: `max` is symmetric and both arguments are U64s, so neither is the subject", peers: Peers(0) },
		{ ..counted("PeersCount", "Peers", 5), left: 6, peers: Peers(0), why: "Peers.roc:24: U64.max: peers: `max` is symmetric" },
		{ ..typed("PeerMethods", "TypedApp", 1, 0), undo: Bool.True, left: 0, peers: Peers(0) },
		# What a rewrite leaves for the sweep, reported and not fixed: an import
		# it made unused, and a line it pushed past 100 columns; KeptImport is
		# MadeUnused's near miss, an import a type still names.
		{ ..plain("MadeUnused"), left: 0, made_unused: 1, made_says: "MadeUnused.roc:4: made_unused_import: `Ref` was used only by the calls the rewrite turned" },
		{ ..plain("KeptImport"), left: 0 },
		{ ..plain("MadeLong"), left: 0, made_long: 1, made_says: "MadeLong.roc:11: made_long_line" },
		{ ..counted("PeerMethodsCount", "PeerMethods", 0), left: 6, peers: Peers(6), why: "PeerMethods.roc:24: peer_receiver: `.max` is symmetric and its receiver and first argument are both U64s, so neither is the subject: U64.max(...)" },
		# A rewrite `roc check` passes and `roc test` refuses puts the file back;
		# a root that fails `roc check` before any rewrite stops the run; a file
		# `roc fmt` would change is not formatted after its rewrite.
		{ ..plain("TestChanged"), left: 2, why: "roc test changed: " },
		{ ..plain("BadBase"), stop: "fails before any rewrite: fix it first" },
		{ ..plain("Unformatted"), left: 0 },
		{ ..plain("Loud"), left: 0 },
	]

	## The plants that hold no call and serve only as roots for other
	## plants: roc-types reads an app, and an app names its platform.
	root_plants : List(Str)
	root_plants = ["TypedApp", "TypedPf", "SecondApp", "PkgApp", "PlantPkg", "Meter", "Shelf", "Book", "Hues"]
}

## A plant run with `types=0` on itself, that must leave nothing.
plain : Str -> Plants.Expect
plain = |name| {
	name,
	file: "",
	beside: "",
	left: 0,
	why: "",
	root: "",
	types: Bool.False,
	confirms: 0,
	rounds: AnyRounds,
	no_types_run: Bool.False,
	unreached: 0,
	stop: "",
	tree_pin: "",
	count: Bool.False,
	proven: 0,
	unsettled: 0,
	types_from: Bool.False,
	peers: AnyPeers,
	undo: Bool.False,
	made_unused: 0,
	made_long: 0,
	made_says: "",
}

## A plant run with the types on `root`, taking `confirms` confirms and
## `rounds` trial rounds.
typed : Str, Str, U64, U64 -> Plants.Expect
typed = |name, root, confirms, rounds| { ..plain(name), root, types: Bool.True, confirms, rounds: Rounds(rounds) }

## A plant `file` counted under `trials=0` on TypedApp, with `proven` calls
## proven and none unsettled.
counted : Str, Str, U64 -> Plants.Expect
counted = |name, file, proven| { ..plain(name), file, root: "TypedApp", types: Bool.True, count: Bool.True, proven }
