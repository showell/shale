## find_clones' self-test: the recipe run over cookbook/c/plants/find_clones/
## and held to the plants' findings exactly: a twin with every name and
## number changed, a run of six lines in two unlike functions, and their
## near misses (a function as long as the twins but of another shape, four
## lines alike that a fifth's string breaks, a test's oracle copy unless
## `tests=1`).
import pf.Shale
import Clones

SelfTest := [].{

	## Both checks, each a stop when it fails, then the PASS line. `find!` is
	## the recipe's own, the findings over a folder.
	run! : (Str, Clones.Settings => Clones.Found) => {}
	run! = |find!| {
		file = "${plants}/clones.c"
		settings = { tests: Bool.False, k: 3, score: 80, min: 8, window: 6 }
		found = find!(plants, settings)
		got = "${joined(found.twins)}\n--\n${joined(found.runs)}"
		twin = "${file}:6-16 sum_weights ~ ${file}:18-28 sum_prices: twin: structure 100%, names 0%"
		run = "${file}:49-54 write_header ~ ${file}:64-69 write_trailer: run: 6 lines alike"
		want = "${twin}\n--\n${run}"
		if got != want {
			Shale.fail!("find_clones self-test: the plants' findings are\n${got}\nthey should be\n${want}")
		}
		# With the tests, the oracle's copy is a twin of each, as written of one.
		oracle = "${plants}/tests/test_clones.c"
		with_tests = joined(find!(plants, { ..settings, tests: Bool.True }).twins)
		want_with_tests = joined([
			"${file}:6-16 sum_weights ~ ${oracle}:5-15 oracle_weights: twin: structure 100%, names 100%",
			"${file}:6-16 sum_weights ~ ${file}:18-28 sum_prices: twin: structure 100%, names 0%",
			"${file}:18-28 sum_prices ~ ${oracle}:5-15 oracle_weights: twin: structure 100%, names 0%",
		])
		if with_tests != want_with_tests {
			Shale.fail!("find_clones self-test: with tests=1 the plants' twins are\n${with_tests}\nthey should be\n${want_with_tests}")
		}
		checks = "the twin and the run over the plants, exactly, their near misses left alone, and a test's oracle only with tests=1"
		Shale.print!("PASS find_clones self-test: ${checks}")
	}
}

## Where the plants are.
plants : Str
plants = "cookbook/c/plants/find_clones"

joined : List(Str) -> Str
joined = |lines| Str.join_with(lines, "\n")
