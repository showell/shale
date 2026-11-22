## The candidates of a file judged by Roc's own types, as roc-types gives
## them by region: a call whose first argument's type is the module's own is
## proven, one of a concrete other type stays, a symmetric call of peers
## stays a module call, and the rest are left to the trial. With
## `peers=undo`, the peer method calls are found here too.
import Candidates
import Peers
import Source

Judging := [].{

	## How many candidates the types judged of each kind, in the order each
	## kind was first counted.
	KindCounts : List((Str, U64))

	## What the types say: each expression's type by `<real path>|<region>`,
	## the aliases the files declare, and the symmetric functions.
	Typing : { by_region : Dict(Str, Str), aliases : Dict(Str, Str), symmetric : Set(Str) }

	## `counts` with one more of `kind`, or `count` more.
	counted : Judging.KindCounts, Str, U64 -> Judging.KindCounts
	counted = |counts, kind, count|
		if counts.any(|(named, _)| named == kind) {
			counts.map(
				|(named, had)| if named == kind {
					(named, had + count)
				} else {
					(named, had)
				},
			)
		} else {
			counts.append((kind, count))
		}

	## Judges each open candidate of the file, whose real path is
	## `real_path`, by its first argument's type, and counts each kind.
	by_types : Judging.KindCounts, Source.File, List(Candidates.Candidate), Str, Judging.Typing -> { counts : Judging.KindCounts, cands : List(Candidates.Candidate) }
	by_types = |counts, file, cands, real_path, typing| {
		var $counts = counts
		var $cands = []
		for cand in cands {
			if cand.why_left == "" {
				judged = judge_one(file, cand, real_path, typing)
				$counts = Judging.counted($counts, judged.kind, 1)
				$cands = $cands.append(judged.cand)
			} else {
				$cands = $cands.append(cand)
			}
		}
		{ counts: $counts, cands: $cands }
	}

	## The peer method calls of the file, `calls`: each whose receiver and
	## first argument are peers, with a module to call through, as a
	## candidate that turns it back (`a.max(b)` to `U64.max(a, b)`), proven,
	## as the receiver's type is where the method was found. A call with an
	## argument that has no span stays, as a module call's does.
	method_peers : Source.File, List(Peers.MethodCall), Str, Judging.Typing -> List(Candidates.Candidate)
	method_peers = |file, calls, real_path, typing| {
		type_at = |place| typing.by_region.get("${real_path}|${Source.node_at(file, place).region}") ?? ""
		calls.join_map(
			|call| {
				verdict = Peers.verdict(type_at(call.receiver), type_at(call.first), typing.aliases)
				if verdict.peers {
					why_left = if call.spanned {
						""
					} else {
						"an argument with no span in the tree, whose text cannot be moved"
					}
					line = Source.line_of_offset(file, Source.node_at(file, call.node).from)
					[Candidates.undoing({ place: call.node, module_name: verdict.module_name, fn: call.fn, line, why_left, type: verdict.type })]
				} else {
					[]
				}
			},
		)
	}
}

## A type's head: a type variable (`a`, `_a`, `a where [...]`), or its name
## (`List` of `List(U8)`, `Access.Viewer` of `Access.Viewer(Topic.At)`), a
## function's (an arrow outside every bracket) `function`, `{...}`'s
## `record`, `[...]`'s `tag union` and `(a, b)`'s `tuple`.
Head : [Variable, Named(Str)]

type_head : Str -> Head
type_head = |type_text| {
	bytes = type_text.to_utf8()
	first = bytes.first() ?? 0
	if Peers.has_top_arrow(bytes) {
		Named("function")
	} else if first == '{' {
		Named("record")
	} else if first == '[' {
		Named("tag union")
	} else if first == '(' {
		Named("tuple")
	} else if first < 'A' or first > 'Z' {
		Variable
	} else {
		length = bytes.find_first_index(|byte| !(is_head_byte(byte))) ?? bytes.len()
		Named(Str.from_utf8_lossy(bytes.take_first(length)))
	}
}

is_head_byte : U8 -> Bool
is_head_byte = |byte|
	(byte >= 'A' and byte <= 'Z') or (byte >= 'a' and byte <= 'z') or (byte >= '0' and byte <= '9') or byte == '_' or byte == '.'

## `word` after its indefinite article: "an" before a vowel sound. A U
## usually sounds like "you" (U8, User), so it takes "an" only in `Un...`
## (Unvalidated).
with_article : Str -> Str
with_article = |word| {
	first = word.to_utf8().first() ?? 0
	vowel = first == 'A' or first == 'E' or first == 'I' or first == 'O' or first == 'a' or first == 'e' or first == 'i' or first == 'o'
	if vowel or word.starts_with("Un") or word.starts_with("un") {
		"an ${word}"
	} else {
		"a ${word}"
	}
}

## One open candidate judged: the kind it is counted as, and the candidate
## with what the types say. A first argument whose type has the module's own
## name, bare, proves the call: roc-types writes a type by the name it is
## declared with, so a qualified head never does, even one whose last
## segment names the module (`Type.Game` is Type's, not Game's).
judge_one : Source.File, Candidates.Candidate, Str, Judging.Typing -> { kind : Str, cand : Candidates.Candidate }
judge_one = |file, cand, real_path, typing| {
	call = Source.node_at(file, cand.node)
	type_at = |number| typing.by_region.get("${real_path}|${Source.kid_node(file, call, number).region}")
	peers =
		if call.kids.len() >= 3 and typing.symmetric.contains(cand.fn) {
			Peers.verdict(type_at(2) ?? "", type_at(3) ?? "", typing.aliases)
		} else {
			{ peers: Bool.False, type: "", module_name: "" }
		}
	if peers.peers {
		why = "peers: `${cand.fn}` is symmetric and both arguments are ${peers.module_name}s, so neither is the subject"
		{ kind: "peers", cand: { ..cand, why_left: why } }
	} else {
		match type_at(2) {
			Err(_) => { kind: "no type", cand: { ..cand, kind: "no type" } }
			Ok(type_text) =>
				match type_head(type_text) {
					Variable => { kind: "a type variable", cand: { ..cand, kind: "a type variable" } }
					Named(head) =>
						if head == cand.module_name {
							{ kind: "the module's type", cand: { ..cand, proven: Bool.True } }
						} else {
							why = "the first argument is ${with_article(head)}, not ${with_article(cand.module_name)}"
							{ kind: "another type", cand: { ..cand, why_left: why } }
						}
				}
		}
	}
}
