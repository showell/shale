## The clone finder over functions read for clones (Shape): the twin pairs,
## two functions of one shape, and the runs of lines alike as written in two
## places. The launcher's header says the rules; this is the Shale
## original's arithmetic, its counts and its order, exactly.
import Shape

Clones := [].{

	## What a search takes: the tests or not, the run length a twin's shape
	## is compared by (`k`), the least structure score, the least counted
	## lines a twin has, and the least run of lines alike (`window`).
	Settings : { tests : Bool, k : I64, score : I64, min : I64, window : I64 }

	## The findings: how many functions were read, and each list's lines,
	## twins highest score first, runs longest first.
	Found : { functions : U64, twins : List(Str), runs : List(Str) }

	## The findings over the functions.
	found : List(Shape.Function), Clones.Settings -> Clones.Found
	found = |functions, settings| {
		twins = find_twins(functions, settings)
		twin_lines = twins.map(
			|twin| {
				pair = "${place(function_at(functions, twin.a))} ~ ${place(function_at(functions, twin.b))}"
				"${pair}: twin: structure ${twin.structure.to_str()}%, names ${twin.names.to_str()}%"
			},
		)
		skip = twins.fold(Set.empty(), |pairs, twin| pairs.insert((twin.a, twin.b)))
		run_lines = find_runs(functions, settings.window, skip).map(
			|run| {
				a = function_at(functions, run.a)
				b = function_at(functions, run.b)
				first = "${a.path}:${run.a_first.to_str()}-${run.a_last.to_str()} ${a.name}"
				second = "${b.path}:${run.b_first.to_str()}-${run.b_last.to_str()} ${b.name}"
				"${first} ~ ${second}: run: ${run.count.to_str()} lines alike"
			},
		)
		{ functions: functions.len(), twins: twin_lines, runs: run_lines }
	}
}

## A run of lines common to more places than this is boilerplate, passed by.
common : U64
common = 8

## The function by its number, counted from 1.
function_at : List(Shape.Function), U64 -> Shape.Function
function_at = |functions, number|
	match functions.get(number - 1) {
		Ok(function) => function
		Err(_) => crash "find_clones: no function ${number.to_str()}"
	}

## A function's place and name, `path:first-last name`.
place : Shape.Function -> Str
place = |function|
	"${function.path}:${function.first.to_str()}-${function.last.to_str()} ${function.name}"

## The texts of each run of `k` lines in a row, in order, each joined by
## newlines: one a start, from the first line to the last that leaves `k`.
windows : List(Str), I64 -> List(Str)
windows = |texts, k| {
	count = texts.len().to_i64_wrap() + 1 - k
	var $out = []
	$start : I64
	var $start = 0
	while $start < count {
		window = if k <= 0 {
			""
		} else {
			Str.join_with(texts.sublist({ start: $start.to_u64_wrap(), len: k.to_u64_wrap() }), "\n")
		}
		$out = $out.append(window)
		$start = $start + 1
	}
	$out
}

## The set of a function's runs of `k` lines, by their shapes or as
## written.
runs_of : Shape.Function, [Shaped, Exact], I64 -> Set(Str)
runs_of = |function, field, k| {
	texts = function.lines.map(
		|line|
			match field {
				Shaped => line.shape
				Exact => line.exact
			},
	)
	windows(texts, k).fold(Set.empty(), |set, window| set.insert(window))
}

## Dice's coefficient of two sets, as a whole percent.
dice : Set(Str), Set(Str) -> U64
dice = |a, b| {
	sizes = a.len() + b.len()
	if sizes == 0 {
		0
	} else {
		shared = a.to_list().count_if(|x| b.contains(x))
		(200 * shared) // sizes
	}
}

## Each pair of the places, the first before the second in the list.
pairs_of : List(a) -> List((a, a))
pairs_of = |places| {
	var $pairs = []
	var $x = 0
	while $x < places.len() {
		var $y = $x + 1
		while $y < places.len() {
			match (places.get($x), places.get($y)) {
				(Ok(p), Ok(q)) => {
					$pairs = $pairs.append((p, q))
				}
				_ => {}
			}
			$y = $y + 1
		}
		$x = $x + 1
	}
	$pairs
}

## Whether a run is in two places or more, but no more than `common`.
is_shared : List(a) -> Bool
is_shared = |places| places.len() >= 2 and places.len() <= common

## A twin pair: the two functions' places, the structure score and the
## names score.
Twin : { a : U64, b : U64, structure : U64, names : U64 }

## The twin pairs, highest structure first, then highest names, then by
## place.
find_twins : List(Shape.Function), Clones.Settings -> List(Twin)
find_twins = |functions, settings| {
	sets = functions.map(
		|function|
			if function.lines.len().to_i64_wrap() >= settings.min {
				runs_of(function, Shaped, settings.k)
			} else {
				Set.empty()
			},
	)
	var $index = Dict.empty()
	$number : U64
	var $number = 0
	for set in sets {
		$number = $number + 1
		for key in set.to_list() {
			$index = $index.insert(key, ($index.get(key) ?? []).append($number))
		}
	}
	var $shared = Dict.empty()
	for (_, places) in $index.to_list() {
		if is_shared(places) {
			for pair in pairs_of(places) {
				$shared = $shared.insert(pair, ($shared.get(pair) ?? 0) + 1)
			}
		}
	}
	var $twins = []
	for ((a, b), count) in $shared.to_list() {
		set_a = sets.get(a - 1) ?? Set.empty()
		set_b = sets.get(b - 1) ?? Set.empty()
		structure = (200 * count) // (set_a.len() + set_b.len())
		if structure.to_i64_wrap() >= settings.score {
			exact_a = runs_of(function_at(functions, a), Exact, settings.k)
			exact_b = runs_of(function_at(functions, b), Exact, settings.k)
			$twins = $twins.append({ a, b, structure, names: dice(exact_a, exact_b) })
		}
	}
	$twins.sort_with(twin_order)
}

twin_order : Twin, Twin -> [Before, Same, After]
twin_order = |x, y|
	first_of([down(x.structure, y.structure), down(x.names, y.names), up(x.a, y.a), up(x.b, y.b)])

## A run of lines alike: the two functions' places, how many lines, and
## each side's first and last line numbers.
Run : { a : U64, b : U64, count : U64, a_first : U64, a_last : U64, b_first : U64, b_last : U64 }

## A window's place: the function's, and the counted line it starts at,
## both from 1.
WindowPlace : { function : U64, start : I64 }

## The runs of `window` or more lines alike as written, in two places,
## longest first; a twin pair's own left out.
find_runs : List(Shape.Function), I64, Set((U64, U64)) -> List(Run)
find_runs = |functions, window, skip| {
	var $at = Dict.empty()
	$number : U64
	var $number = 0
	for function in functions {
		$number = $number + 1
		$start : I64
		var $start = 0
		for text in windows(function.lines.map(|line| line.exact), window) {
			$start = $start + 1
			here = { function: $number, start: $start }
			$at = $at.insert(text, ($at.get(text) ?? []).append(here))
		}
	}
	var $pairs = Dict.empty()
	for (_, places) in $at.to_list() {
		if is_shared(places) {
			for (p, q) in pairs_of(places) {
				offset = q.start - p.start
				# A window against itself shifted is no second place.
				if !(p.function == q.function and offset < window) {
					key = (p.function, q.function, offset)
					$pairs = $pairs.insert(key, ($pairs.get(key) ?? []).append(p.start))
				}
			}
		}
	}
	var $runs = []
	for ((a, b, offset), starts) in $pairs.to_list() {
		if !(skip.contains((a, b))) {
			sides = { a: function_at(functions, a), b: function_at(functions, b), a_place: a, b_place: b }
			$runs = $runs.concat(runs_from(sides, { window, offset, starts: starts.sort_with(up_signed) }))
		}
	}
	$runs.sort_with(run_order)
}

## The runs one pair of functions at one offset holds: each stretch of
## starts one after another is a run.
runs_from : { a : Shape.Function, b : Shape.Function, a_place : U64, b_place : U64 }, { window : I64, offset : I64, starts : List(I64) } -> List(Run)
runs_from = |sides, { window, offset, starts }| {
	line_at = |function, number| {
		counted = function.lines.get((number - 1).to_u64_wrap()) ?? { line: 0, exact: "", shape: "" }
		counted.line
	}
	stretch = |first, last| {
		a: sides.a_place,
		b: sides.b_place,
		count: (last - first + window).to_u64_wrap(),
		a_first: line_at(sides.a, first),
		a_last: line_at(sides.a, last + window - 1),
		b_first: line_at(sides.b, first + offset),
		b_last: line_at(sides.b, last + window - 1 + offset),
	}
	match starts {
		[] => []
		[head, .. as rest] => {
			var $runs = []
			var $first = head
			var $last = head
			for start in rest {
				if start == $last + 1 {
					$last = start
				} else {
					$runs = $runs.append(stretch($first, $last))
					$first = start
					$last = start
				}
			}
			$runs.append(stretch($first, $last))
		}
	}
}

run_order : Run, Run -> [Before, Same, After]
run_order = |x, y|
	first_of([down(x.count, y.count), up(x.a, y.a), up(x.a_first, y.a_first), up(x.b, y.b), up(x.b_first, y.b_first)])

## The first of the orders that is not `Same`.
first_of : List([Before, Same, After]) -> [Before, Same, After]
first_of = |orders| orders.find_first(|order| order != Same) ?? Same

## Two numbers, the smaller first.
up : U64, U64 -> [Before, Same, After]
up = |x, y|
	if x < y {
		Before
	} else if x > y {
		After
	} else {
		Same
	}

## Two signed numbers, the smaller first.
up_signed : I64, I64 -> [Before, Same, After]
up_signed = |x, y|
	if x < y {
		Before
	} else if x > y {
		After
	} else {
		Same
	}

## Two numbers, the larger first.
down : U64, U64 -> [Before, Same, After]
down = |x, y| up(y, x)
