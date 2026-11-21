## list_long_functions' self-test: the recipe run over
## cookbook/c/plants/list_long_functions/ and held to the plants' known
## lengths, exactly: the distribution, the default threshold, an `over=` that
## a function's comments and blank lines would pass but its code does not,
## and `tests=1`.
import pf.Shale
import Length

SelfTest := [].{

	## Every check, each a stop when it fails, then the PASS line. `measure!`
	## is the recipe's own, a folder's functions (and tests, when asked)
	## measured.
	run! : (Str, Bool => List(Length.Measured)) => {}
	run! = |measure!| {
		file = "${plants}/lengths.c"
		functions = measure!(plants, Bool.False)
		default = listed!(functions, Percentile)
		same!("the distribution", default.summary, "4 functions: median 2, p75 4, p90 6, p95 6, p99 6, max 6 code lines")
		same!("the default threshold", default.threshold.to_str(), "6")
		same!("the list past the 95th percentile", joined(default.lines), "")
		past_three = [
			"${file}:25: long: six_lines: 6 code lines (1 comment, 0 blank)",
			"${file}:13: long: four_with_comments: 4 code lines (3 comment, 2 blank)",
		]
		same!("the list past 3", joined(listed!(functions, Given(3)).lines), joined(past_three))
		# Nine lines of body, four of them code: not past 4.
		past_four = "${file}:25: long: six_lines: 6 code lines (1 comment, 0 blank)"
		same!("the list past 4", joined(listed!(functions, Given(4)).lines), past_four)
		with_tests = listed!(measure!(plants, Bool.True), Given(6))
		past_six = "${file}:35: long: a_long_test: 8 code lines (0 comment, 0 blank)"
		same!("the list past 6 with the tests", joined(with_tests.lines), past_six)
		checks = "6 checks, the distribution, the default threshold and three lists over the plants"
		Shale.print!("PASS list_long_functions self-test: ${checks}")
	}
}

## Where the plants are.
plants : Str
plants = "cookbook/c/plants/list_long_functions"

## The listing, or the self-test stopped.
listed! : List(Length.Measured), [Given(U64), Percentile] => { summary : Str, threshold : U64, lines : List(Str) }
listed! = |measured, limit|
	match Length.listing(measured, limit) {
		Ok(listing) => listing
		Err(why) => {
			Shale.fail!(why)
			{ summary: "", threshold: 0, lines: [] }
		}
	}

## `got` equal to `want`, or the self-test stops, naming `what`.
same! : Str, Str, Str => {}
same! = |what, got, want|
	if got != want {
		Shale.fail!("list_long_functions self-test: ${what} is\n${got}\nit should be\n${want}")
	}

joined : List(Str) -> Str
joined = |lines| Str.join_with(lines, "\n")
