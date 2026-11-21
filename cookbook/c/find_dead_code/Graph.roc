## The tree's names as a graph, and what no program reaches. Every
## function, test, test list, data item, macro, generator and type is a
## node; its edges are the nodes its words name. A word resolves to its own
## file's node of that name when the file has one, else to every node of
## that name in the tree, never to the node itself. A generator answers to
## its type's name and to each function its macro makes (`Ints`,
## `Ints_make`, `Ints_push`). The roots are the programs: production's each
## `main` outside the tests, the tests' each test, test list and `main` of
## theirs.
import items.Item
import items.Words

Graph := [].{

	## A node: where its item is, its kind and name, whether its file is the
	## tests', the words it names, and a generator's macro or a macro's own
	## functions (`make`, `push`).
	Node : {
		path : Str,
		line : U64,
		kind : Item.Kind,
		name : Str,
		test : Bool,
		words : List(Str),
		macro : Str,
		makes : List(Str),
	}

	## A finding: where it is, its rule (`dead` or `test_only`) and its text.
	Finding : { path : Str, line : U64, rule : Str, text : Str }

	## The findings over the nodes, sorted by place: each function (not a
	## `main`) and generator no program reaches, `dead`, and each outside
	## the tests' files that only the tests reach, `test_only`.
	findings : List(Graph.Node) -> List(Graph.Finding)
	findings = |nodes| {
		edges = edges_of(nodes, index_of(nodes))
		roots = roots_of(nodes)
		in_production = reached(edges, roots.production)
		in_tests = reached(edges, roots.tests)
		users = users_of(edges)
		var $found = []
		$at : U64
		var $at = 0
		for node in nodes {
			is_judged = (node.kind == Function and node.name != "main") or node.kind == Generator
			rule =
				if !(is_judged) or is_marked(in_production, $at) {
					""
				} else if is_marked(in_tests, $at) {
					if node.test {
						""
					} else {
						"test_only"
					}
				} else {
					"dead"
				}
			if rule != "" {
				generated =
					if node.kind == Generator {
						" (a ${node.macro} generator)"
					} else {
						""
					}
				named_by =
					match users.get($at) {
						Ok(who) if rule == "dead" => " (named only by unreached code: ${user_names(nodes, who)})"
						_ => ""
					}
				$found = $found.append({ path: node.path, line: node.line, rule, text: "${node.name}${generated}${named_by}" })
			}
			$at = $at + 1
		}
		$found.sort_with(place_order)
	}

	## A finding's line: `path:line: rule: text`.
	finding_line : Graph.Finding -> Str
	finding_line = |found| "${found.path}:${found.line.to_str()}: ${found.rule}: ${found.text}"
}

## The names to their nodes, each node's place counted from 0: `by_name`
## every node of a name, `by_file` a file's own node of it (the last, when
## the file has two).
Index : { by_name : Dict(Str, List(U64)), by_file : Dict((Str, Str), U64) }

## The names each node answers to, indexed: its own, and a generator's
## functions. A test list answers to none: its name is its macro's
## (`TEST_MAIN`), and a list is a root.
index_of : List(Graph.Node) -> Index
index_of = |nodes| {
	makes = nodes.fold(
		Dict.empty(),
		|so_far, node|
			if node.kind == Macro {
				so_far.insert(node.name, node.makes)
			} else {
				so_far
			},
	)
	var $by_name = Dict.empty()
	var $by_file = Dict.empty()
	$at : U64
	var $at = 0
	for node in nodes {
		if node.name != "" and node.kind != TestList {
			made =
				if node.kind == Generator {
					(makes.get(node.macro) ?? []).map(|suffix| "${node.name}_${suffix}")
				} else {
					[]
				}
			for name in made.prepend(node.name) {
				$by_name = $by_name.insert(name, ($by_name.get(name) ?? []).append($at))
				$by_file = $by_file.insert((node.path, name), $at)
			}
		}
		$at = $at + 1
	}
	{ by_name: $by_name, by_file: $by_file }
}

## Each node's edges, the nodes its words resolve to, in its words' order.
edges_of : List(Graph.Node), Index -> List(List(U64))
edges_of = |nodes, index|
	nodes.map_with_index(|node, at| node.words.fold([], |out, word| out.concat(resolved(index, { node, at, word }))))

## The nodes a word of the node at `at` resolves to: its file's own of that
## name, else every one of it, never the node itself.
resolved : Index, { node : Graph.Node, at : U64, word : Str } -> List(U64)
resolved = |index, { node, at, word }| {
	named =
		match index.by_file.get((node.path, word)) {
			Ok(own) => [own]
			Err(_) => index.by_name.get(word) ?? []
		}
	named.keep_if(|target| target != at)
}

## The roots: production's, each `main` outside the tests; the tests', each
## test, test list and `main` of theirs.
roots_of : List(Graph.Node) -> { production : List(U64), tests : List(U64) }
roots_of = |nodes| {
	var $production = []
	var $tests = []
	$at : U64
	var $at = 0
	for node in nodes {
		if node.kind == Function and node.name == "main" {
			if node.test {
				$tests = $tests.append($at)
			} else {
				$production = $production.append($at)
			}
		} else if node.kind == Test or node.kind == TestList {
			$tests = $tests.append($at)
		}
		$at = $at + 1
	}
	{ production: $production, tests: $tests }
}

## Which nodes the roots reach, a mark a node by its place.
reached : List(List(U64)), List(U64) -> List(Bool)
reached = |edges, roots| {
	var $marks = List.repeat(Bool.False, edges.len())
	var $work = []
	for root in roots {
		if !(is_marked($marks, root)) {
			$marks = marked($marks, root)
			$work = $work.append(root)
		}
	}
	$next : U64
	var $next = 0
	while $next < $work.len() {
		at = $work.get($next) ?? 0
		$next = $next + 1
		for target in edges.get(at) ?? [] {
			if !(is_marked($marks, target)) {
				$marks = marked($marks, target)
				$work = $work.append(target)
			}
		}
	}
	$marks
}

is_marked : List(Bool), U64 -> Bool
is_marked = |marks, at| marks.get(at) ?? Bool.False

marked : List(Bool), U64 -> List(Bool)
marked = |marks, at|
	match marks.set(at, Bool.True) {
		Ok(now) => now
		Err(_) => crash "find_dead_code: an edge past the last node"
	}

## Who names each node: a node's place to the places of the nodes whose
## edges reach it, once for each edge.
users_of : List(List(U64)) -> Dict(U64, List(U64))
users_of = |edges| {
	var $users = Dict.empty()
	$at : U64
	var $at = 0
	for out in edges {
		for target in out {
			$users = $users.insert(target, ($users.get(target) ?? []).append($at))
		}
		$at = $at + 1
	}
	$users
}

## The names of the nodes at the places, sorted, each once, joined by `, `.
user_names : List(Graph.Node), List(U64) -> Str
user_names = |nodes, places| {
	names = places
		.map(|place| (nodes.get(place) ?? no_node).name)
		.sort_with(Words.byte_order)
	once = names.fold(
		[],
		|kept, name|
			if kept.last() == Ok(name) {
				kept
			} else {
				kept.append(name)
			},
	)
	Str.join_with(once, ", ")
}

## The node a place past the last would be, which no edge reaches.
no_node : Graph.Node
no_node = { path: "", line: 0, kind: Function, name: "", test: Bool.False, words: [], macro: "", makes: [] }

## Findings by path, in the order of their bytes, then by line.
place_order : Graph.Finding, Graph.Finding -> [Before, Same, After]
place_order = |x, y|
	match Words.byte_order(x.path, y.path) {
		Same =>
			if x.line < y.line {
				Before
			} else if x.line > y.line {
				After
			} else {
				Same
			}
		other => other
	}
