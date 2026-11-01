## **A ROC FILE'S TREE AS roc-parser SEES IT, WRITTEN AS roc-ast WRITES ITS
## OWN**: for each path on stdin (one a line), `== <path>` and the file's
## tree in roc-ast's `--flat` shape (tools/roc_ast/main.zig), so Shale's
## `roc_tree` reads either. An experiment: roc-parser
## (github.com/lukewilliamboswell/roc-parser, its `RocSyntax`) is a pure-Roc
## parser of Roc with lossless tokens and byte spans; roc-ast is the
## compiler's own. Nothing here replaces roc-ast or changes a recipe's default.
##
## The first line is `roc-parser flat 1 <roc-parser version>`, then:
##   `n<TAB>parent<TAB>kind<TAB>line<TAB>col<TAB>end_line<TAB>end_col`
##   `a<TAB>node<TAB>name<TAB>value`
## Nodes are numbered from 1 in each file, a node before its children, in
## source order; `parent` 0 is the root's; a node written with no span (a
## compiler wrapper: `args`, `receiver`, `field`) has 0s. Lines and columns
## are 1-based, the end column one past the last byte, as the compiler's.
## Values escape `\`, tab, newline and carriage return as roc-ast's do.
##
## **THE KINDS ARE THE COMPILER'S WHERE THE RECIPES READ THEM** (the thin
## layer, `kind_of` and the shapes below), each node's own roc-parser kind
## kept as its `rp` attribute, and a kind with no compiler match written
## `rp-<Kind>`. roc-parser stores no strings, so names are read from the
## source by span: `raw` (an identifier, a literal, a qualified name),
## `method`, `field`, `op`. Where the mapping guesses, it says so:
##   - **A CALL'S CALLEE `a.b(...)`**: roc-parser has only `Call(FieldAccess
##     ...)`. A receiver that is a chain of capitalised names (`Str.concat`,
##     `Principal.HumanId.from_u64`) is a qualified name: `e-apply` of an
##     `e-ident` whose `raw` is the whole path, as the compiler's. Any other
##     receiver makes a method call: `e-method-call` (`method ".concat"`),
##     its `receiver` and `args` wrappers. The guess: a capitalised name is
##     a module or a type, never a value.
##   - **A CAPITALISED IDENTIFIER IS A TAG** (`e-tag`), and a path ending in
##     one (`Principal.Id.Human`) a qualified tag: roc-parser has no tag kind.
##   - **PARENTHESES ARE A ONE-ITEM TUPLE** (`e-tuple`), as the compiler
##     writes them.
## A file roc-parser does not parse cleanly (`status` not `Complete`) stops
## the run with `<path>: roc-parser: ...` and exit 1, as roc-ast does.
app [main!] {
	cli: platform "https://github.com/roc-lang/basic-cli/releases/download/0.25.0/CZsY7tYZwR3rj9kYbpaCfxki2yVAaRL8bBwMLvB2xkbA.tar.zst",
	parser: "../../../roc-parser/package/main.roc",
}

import cli.Stdin
import cli.Stdout
import cli.Path
import parser.RocSyntax

## roc-parser's version, as `git describe` names its checkout beside Shale.
roc_parser_version : Str
roc_parser_version = "3.0.0-3-gbafa803"

main! = |_args| {
	input = Stdin.read_to_end!() ?? []
	paths = Str.from_utf8_lossy(input).split_on("\n").keep_if(|p| p != "")
	Stdout.write!("roc-parser flat 1 ${roc_parser_version}\n")?
	each_file!(paths)
}

## Each path's tree, in order, until one that does not read or parse.
each_file! : List(Str) => Try({}, _)
each_file! = |paths|
	match paths {
		[] => Ok({})
		[path, .. as rest] =>
			match Path.unix(path).read_bytes!() {
				Err(_) => {
					Stdout.line!("${path}: roc-parser: the file does not read")?
					Err(Exit(1))
				}
				Ok(bytes) => {
					parsed = RocSyntax.parse_bytes(bytes)
					if parsed.status == Complete {
						Stdout.write_bytes!(flat(path, parsed))?
						each_file!(rest)
					} else {
						Stdout.line!("${path}: roc-parser: ${refusal(parsed)}")?
						Err(Exit(1))
					}
				}
			}
	}

## Why a parse is refused: its status, and its first diagnostic's place.
refusal : RocSyntax.Parsed -> Str
refusal = |parsed| {
	starts = line_starts(parsed.source)
	match parsed.diagnostics.first() {
		Ok(d) => {
			(line, col) = line_col(starts, d.start)
			"${Str.inspect(parsed.status)}: ${Str.inspect(d.code)} at ${line.to_str()}:${col.to_str()}"
		}
		Err(_) => Str.inspect(parsed.status)
	}
}

## What a file's walk needs: the parse, and each line's first byte.
Ctx : { p : RocSyntax.Parsed, starts : List(U64) }

## The lines written so far, and the last node's number.
St : { out : List(U8), count : U64 }

## A file's `== path` and its nodes, from the root.
flat : Str, RocSyntax.Parsed -> List(U8)
flat = |path, parsed| {
	ctx = { p: parsed, starts: line_starts(parsed.source) }
	head = { out: "== ${escape(path)}\n".to_utf8(), count: 0 }
	walk(ctx, head, 0, parsed.root).out
}

## Each line's first byte offset (0-based): 0, and one past each newline.
line_starts : List(U8) -> List(U64)
line_starts = |source| {
	var $starts = [0]
	var $i = 0
	for b in source {
		$i = $i + 1
		if b == '\n' {
			$starts = $starts.append($i)
		}
	}
	$starts
}

## The 1-based line and column of byte `offset`.
line_col : List(U64), U64 -> (U64, U64)
line_col = |starts, offset| {
	line = last_at_most(starts, offset, 0, starts.len())
	start = starts.get(line) ?? 0
	(line + 1, offset - start + 1)
}

## The index of the last start at most `offset`, in `lo..hi` (binary search).
last_at_most : List(U64), U64, U64, U64 -> U64
last_at_most = |starts, offset, lo, hi|
	if hi - lo <= 1 {
		lo
	} else {
		mid = lo + (hi - lo) // 2
		if (starts.get(mid) ?? 0) <= offset {
			last_at_most(starts, offset, mid, hi)
		} else {
			last_at_most(starts, offset, lo, mid)
		}
	}

## A value as the format writes it: `\`, tab, newline and CR escaped.
escape : Str -> Str
escape = |s|
	s.replace_each("\\", "\\\\").replace_each("\t", "\\t").replace_each("\n", "\\n").replace_each("\r", "\\r")

## The node at index `i`, which the parse holds.
node_at : Ctx, U64 -> RocSyntax.Node
node_at = |ctx, i|
	match ctx.p.nodes.get(i) {
		Ok(n) => n
		Err(_) => crash "roc-parser-flat: a child index names a node the parse holds"
	}

## The node's children's indexes, in order.
kids_of : Ctx, RocSyntax.Node -> List(U64)
kids_of = |ctx, n| ctx.p.children.sublist({ start: n.children_start, len: n.children_len })

## The source text a node spans.
text : Ctx, RocSyntax.Node -> Str
text = |ctx, n| Str.from_utf8_lossy(ctx.p.source.sublist({ start: n.start, len: n.end - n.start }))

## A node line (its span, or none), its `rp` attribute when it has a native
## kind; the new node's number.
put : St,
Ctx,
{
	parent : U64,
	kind : Str,
	span : [Span(RocSyntax.Node), NoSpan],
} -> (St, U64)
put = |st, ctx, { parent, kind, span }| {
	id = st.count + 1
	place =
		match span {
			NoSpan => "0\t0\t0\t0"
			Span(n) => {
				(l, c) = line_col(ctx.starts, n.start)
				(el, ec) = line_col(ctx.starts, n.end)
				"${l.to_str()}\t${c.to_str()}\t${el.to_str()}\t${ec.to_str()}"
			}
		}
	line = "n\t${parent.to_str()}\t${escape(kind)}\t${place}\n"
	rp =
		match span {
			NoSpan => ""
			Span(n) => "a\t${id.to_str()}\trp\t${Str.inspect(n.kind)}\n"
		}
	({ out: st.out.concat(line.to_utf8()).concat(rp.to_utf8()), count: id }, id)
}

## An attribute line on node `id`.
attr : St, U64, Str, Str -> St
attr = |st, id, name, value|
	{ ..st, out: st.out.concat("a\t${id.to_str()}\t${name}\t${escape(value)}\n".to_utf8()) }

## The node at `i` and all under it, below node number `parent`.
walk : Ctx, St, U64, U64 -> St
walk = |ctx, st, parent, i| {
	n = node_at(ctx, i)
	kids = kids_of(ctx, n)
	match n.kind {
		# Wrappers the compiler has no node for: their children stand in their place.
		ExpressionStatement | Interpolation => kids.fold(st, |s, k| walk(ctx, s, parent, k))
		TagPattern => named(ctx, st, { parent, kind: "p-tag", name: "raw", n, kids })
		PatternField => named(ctx, st, { parent, kind: "field", name: "field", n, kids })
		TypeField => named(ctx, st, { parent, kind: "anno-record-field", name: "name", n, kids })
		TypeHeader => named(ctx, st, { parent, kind: "header", name: "name", n, kids })
		Call => call(ctx, st, parent, n, kids)
		FieldAccess => field_access(ctx, st, parent, n, kids)
		Identifier => {
			raw = text(ctx, n)
			(s1, id) = put(st, ctx, { parent, kind: if (is_capital(raw)) "e-tag" else "e-ident", span: Span(n) })
			attr(s1, id, "raw", raw)
		}
		Lambda => {
			(s1, id) = put(st, ctx, { parent, kind: "e-lambda", span: Span(n) })
			params = kids.drop_last(1)
			(s2, args) = put(s1, ctx, { parent: id, kind: "args", span: NoSpan })
			s3 = params.fold(s2, |s, k| walk(ctx, s, args, k))
			kids.drop_first(params.len()).fold(s3, |s, k| walk(ctx, s, id, k))
		}
		RecordField => {
			(s1, id) = put(st, ctx, { parent, kind: "field", span: NoSpan })
			with_name =
				match kids.first() {
					Ok(k) => attr(s1, id, "field", text(ctx, node_at(ctx, k)))
					Err(_) => s1
				}
			kids.drop_first(1).fold(with_name, |s, k| walk(ctx, s, id, k))
		}
		NominalApply => {
			# `Name.(x)`: the compiler wraps the type's name in `mapper`.
			(s1, id) = put(st, ctx, { parent, kind: "e-nominal-apply", span: Span(n) })
			match kids {
				[name, .. as rest] => {
					(s2, mapper) = put(s1, ctx, { parent: id, kind: "mapper", span: NoSpan })
					s3 = walk(ctx, s2, mapper, name)
					rest.fold(s3, |s, k| walk(ctx, s, id, k))
				}
				[] => s1
			}
		}
		Unary => {
			# The compiler writes no span on a unary, its operator as `value`.
			(s1, id) = put(st, ctx, { parent, kind: "unary", span: NoSpan })
			s2 =
				match kids.first() {
					Ok(k) => attr(s1, id, "value", Str.from_utf8_lossy(ctx.p.source.sublist({ start: n.start, len: node_at(ctx, k).start - n.start })).trim())
					Err(_) => s1
				}
			kids.fold(s2, |s, k| walk(ctx, s, id, k))
		}
		Binary => {
			(s1, id) = put(st, ctx, { parent, kind: "e-binop", span: Span(n) })
			s2 =
				match kids {
					[l, r, ..] => attr(s1, id, "op", between(ctx, node_at(ctx, l), node_at(ctx, r)))
					_ => s1
				}
			kids.fold(s2, |s, k| walk(ctx, s, id, k))
		}
		_ => {
			kind = kind_of(n, text(ctx, n))
			(s1, id) = put(st, ctx, { parent, kind, span: Span(n) })
			s2 = if (has_raw(n.kind)) attr(s1, id, "raw", text(ctx, n)) else s1
			kids.fold(s2, |s, k| walk(ctx, s, id, k))
		}
	}
}

## **A NODE NAMED BY ITS FIRST CHILD** (a tag pattern's tag, a record
## type's field, a type's header): the compiler writes the name as an
## attribute, not a node, so the first child is that attribute and the rest
## are children.
named : Ctx,
St,
{
	parent : U64,
	kind : Str,
	name : Str,
	n : RocSyntax.Node,
	kids : List(U64),
} -> St
named = |ctx, st, { parent, kind, name, n, kids }| {
	(s1, id) = put(st, ctx, { parent, kind, span: Span(n) })
	match kids {
		[first, .. as rest] => rest.fold(attr(s1, id, name, path_text(ctx, first)), |s, k| walk(ctx, s, id, k))
		[] => s1
	}
}

## The source between two nodes, trimmed: a binary operator.
between : Ctx, RocSyntax.Node, RocSyntax.Node -> Str
between = |ctx, l, r|
	if r.start < l.end {
		""
	} else {
		Str.from_utf8_lossy(ctx.p.source.sublist({ start: l.end, len: r.start - l.end })).trim()
	}

## **A CALL**: a method call when its callee is `x.name` and `x` is no
## qualified name (the guess the header names); else an application of
## the callee as walked (a qualified name, a tag, a function).
call : Ctx, St, U64, RocSyntax.Node, List(U64) -> St
call = |ctx, st, parent, n, kids|
	match kids {
		[callee, .. as args] => {
			c = node_at(ctx, callee)
			match (c.kind, kids_of(ctx, c)) {
				(FieldAccess, [recv, name]) if !is_capital_path(ctx, recv) => {
					(s1, id) = put(st, ctx, { parent, kind: "e-method-call", span: Span(n) })
					s2 = attr(s1, id, "method", ".${text(ctx, node_at(ctx, name))}")
					(s3, receiver) = put(s2, ctx, { parent: id, kind: "receiver", span: NoSpan })
					s4 = walk(ctx, s3, receiver, recv)
					(s5, wrapped) = put(s4, ctx, { parent: id, kind: "args", span: NoSpan })
					args.fold(s5, |s, k| walk(ctx, s, wrapped, k))
				}
				_ => {
					(s1, id) = put(st, ctx, { parent, kind: "e-apply", span: Span(n) })
					kids.fold(s1, |s, k| walk(ctx, s, id, k))
				}
			}
		}
		[] => put(st, ctx, { parent, kind: "rp-Call", span: Span(n) }).0
	}

## **`x.name` OUTSIDE A CALL'S CALLEE**, or a qualified callee: a chain of
## capitalised names before `name` is a qualified name (`e-ident`, or
## `e-tag` when `name` is capitalised too), its `raw` the path; any other
## receiver a field access, as the compiler writes it.
field_access : Ctx, St, U64, RocSyntax.Node, List(U64) -> St
field_access = |ctx, st, parent, n, kids|
	match kids {
		[recv, name] if is_capital_path(ctx, recv) => {
			last = text(ctx, node_at(ctx, name))
			raw = "${path_text(ctx, recv)}.${last}"
			(s1, id) = put(st, ctx, { parent, kind: if (is_capital(last)) "e-tag" else "e-ident", span: Span(n) })
			attr(s1, id, "raw", raw)
		}
		[_, _] => {
			# `a.b.c`: one access, its receiver the chain's start and a
			# segment a name, as the compiler writes it (roc-parser nests one
			# access a dot).
			(base, names) = chain(ctx, n, [])
			# `t.0`: the compiler's tuple access, by a number.
			kind = if (names.all(|x| x.to_utf8().all(|b| b >= '0' and b <= '9'))) "e-tuple-access" else "e-field-access"
			(s1, id) = put(st, ctx, { parent, kind, span: Span(n) })
			(s2, receiver) = put(s1, ctx, { parent: id, kind: "receiver", span: NoSpan })
			s3 = walk(ctx, s2, receiver, base)
			names.fold(
				s3,
				|s, name| {
					(s4, segment) = put(s, ctx, { parent: id, kind: "segment", span: NoSpan })
					attr(attr(s4, segment, "mode", "required"), segment, "field", name)
				},
			)
		}
		_ => {
			(s1, id) = put(st, ctx, { parent, kind: "rp-FieldAccess", span: Span(n) })
			kids.fold(s1, |s, k| walk(ctx, s, id, k))
		}
	}

## A field access chain's start and its names in order: through each
## access whose receiver is itself a field access of a value.
chain : Ctx, RocSyntax.Node, List(Str) -> (U64, List(Str))
chain = |ctx, n, after|
	match kids_of(ctx, n) {
		[recv, name] => {
			names = [text(ctx, node_at(ctx, name))].concat(after)
			r = node_at(ctx, recv)
			# A receiver that is itself a qualified name (`Rules.poop` in
			# `Rules.poop.x`) is the chain's start, as the compiler's `raw`.
			if r.kind == FieldAccess and !is_capital_path(ctx, recv) and !qualified(ctx, r) {
				chain(ctx, r, names)
			} else {
				(recv, names)
			}
		}
		_ => crash "roc-parser-flat: a field access has a receiver and a name"
	}

## Whether a field access is a qualified name: its receiver a capital chain.
qualified : Ctx, RocSyntax.Node -> Bool
qualified = |ctx, n|
	match kids_of(ctx, n) {
		[recv, _] => is_capital_path(ctx, recv)
		_ => Bool.False
	}

## Whether the node is a capitalised name, or a chain of them (`A.B`).
is_capital_path : Ctx, U64 -> Bool
is_capital_path = |ctx, i| {
	n = node_at(ctx, i)
	match (n.kind, kids_of(ctx, n)) {
		(Identifier, _) => is_capital(text(ctx, n))
		(FieldAccess, [recv, name]) => is_capital_path(ctx, recv) and is_capital(text(ctx, node_at(ctx, name)))
		_ => Bool.False
	}
}

## A capital chain's names, joined by `.`, spaces left out.
path_text : Ctx, U64 -> Str
path_text = |ctx, i| {
	n = node_at(ctx, i)
	match (n.kind, kids_of(ctx, n)) {
		(FieldAccess, [recv, name]) => "${path_text(ctx, recv)}.${text(ctx, node_at(ctx, name))}"
		_ => text(ctx, n)
	}
}

## Whether a name starts with a capital letter.
is_capital : Str -> Bool
is_capital = |s|
	match s.to_utf8().first() {
		Ok(b) => b >= 'A' and b <= 'Z'
		Err(_) => Bool.False
	}

## The kinds whose whole text is their `raw`, as the compiler gives it.
has_raw : RocSyntax.NodeKind -> Bool
has_raw = |k|
	match k {
		NumberLiteral | StringSegment | IdentifierPattern | CharacterLiteral => Bool.True
		_ => Bool.False
	}

## **THE COMPILER'S KIND FOR A roc-parser KIND**, where one matches; else
## `rp-<Kind>`. `t` is the node's text (a number with a `.` is a fraction;
## a suffix ending `?` a question suffix).
kind_of : RocSyntax.Node, Str -> Str
kind_of = |n, t|
	match n.kind {
		File => "file"
		Declaration => "s-decl"
		VariableDeclaration => "s-var"
		Expect => "s-expect"
		For => "s-for"
		While => "s-while"
		Return => "e-return"
		Break => "s-break"
		Import => "s-import"
		TypeAnnotation => "s-type-anno"
		TypeDeclaration => "s-type-decl"
		AssociatedBlock => "associated"
		If => if (n.children_len == 2) "e-if-without-else" else "e-if-then-else"
		NominalRecord => "e-nominal-record"
		Match => "e-match"
		MatchBranch => "branch"
		Block => "e-block"
		Record => "e-record"
		RecordUpdate => "e-record"
		ListLiteral => "e-list"
		Tuple => "e-tuple"
		Parenthesized => "e-tuple"
		ArrowCall => "e-arrow-call"
		MethodArrowCall => "e-arrow-call"
		Unary => "unary"
		Crash => "e-crash"
		Debug => "e-dbg"
		Ellipsis => "e-ellipsis"
		StringLiteral => "e-string"
		MultilineStringLiteral => "e-multiline-string"
		StringSegment => "e-string-part"
		CharacterLiteral => "e-single-quote"
		NumberLiteral => if (t.contains(".")) "e-frac" else "e-int"
		Suffix => if (t.ends_with("?")) "e-question-suffix" else "rp-Suffix"
		IdentifierPattern => "p-ident"
		WildcardPattern => "p-underscore"
		TagPattern => "p-tag"
		RecordPattern => "p-record"
		RestPattern => "p-list-rest"
		TypedLiteral => "e-typed-int"
		ListPattern => "p-list"
		TuplePattern => "p-tuple"
		TypeName => "ty"
		TypeVariable => "ty-var"
		TypeFunction => "ty-fn"
		TypeApply => "ty-apply"
		TypeRecord => "ty-record"
		TypeTagUnion => "ty-tag-union"
		TypeTuple => "ty-tuple"
		TypeParenthesized => "rp-TypeParenthesized"
		other => "rp-${Str.inspect(other)}"
	}
