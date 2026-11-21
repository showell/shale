## list_deep_nesting's self-test: the recipe run over
## cookbook/c/plants/list_deep_nesting/ and held to every plant's known depth
## and line, exactly: depths 3, 4 and 5, braces in a string, a character
## literal and a comment, a braceless `if`, an initializer's braces, the
## default `min`, and `tests=1`.
import pf.Shale
import Depth

SelfTest := [].{

	## Every check, each a stop when it fails, then the PASS line. `measure!`
	## is the recipe's own, a folder's functions (and tests, when asked)
	## measured.
	run! : (Str, Bool => List(Depth.Measured)) => {}
	run! = |measure!| {
		file = "${plants}/depths.c"
		functions = measure!(plants, Bool.False)
		every_depth = [
			"${file}:52: depth_five: depth 5 (at line 57)",
			"${file}:40: depth_four: depth 4 (at line 44)",
			"${file}:28: depth_three: depth 3 (at line 31)",
			# The braces in its comment, string and character literal are none.
			"${file}:19: braces_in_text: depth 2 (at line 21)",
			"${file}:6: depth_one: depth 1 (at line 6)",
			# Its braceless `if`s and its initializer's braces are no level.
			"${file}:10: braceless: depth 1 (at line 11)",
		]
		same!("every function's depth", joined(listed!(functions, 1).lines), joined(every_depth))
		default = listed!(functions, 4)
		at_four = [
			"${file}:52: depth_five: depth 5 (at line 57)",
			"${file}:40: depth_four: depth 4 (at line 44)",
		]
		same!("the list at the default min", joined(default.lines), joined(at_four))
		same!("the summary", default.summary, "6 functions read, 2 at depth 4 or past; by depth, 1: 2, 2: 1, 3: 1, 4: 1, 5: 1")
		with_tests = listed!(measure!(plants, Bool.True), 2)
		same!("the summary with the tests", with_tests.summary, "7 functions read, 5 at depth 2 or past; by depth, 1: 2, 2: 2, 3: 1, 4: 1, 5: 1")
		fifth = with_tests.lines.get(4) ?? "<none>"
		same!("the test's depth", fifth, "${file}:69: a_nested_test: depth 2 (at line 70)")
		checks = "5 checks, every plant's depth, the default min, the summaries and a test over the plants"
		Shale.print!("PASS list_deep_nesting self-test: ${checks}")
	}
}

## Where the plants are.
plants : Str
plants = "cookbook/c/plants/list_deep_nesting"

## The listing, or the self-test stopped.
listed! : List(Depth.Measured), I64 => { summary : Str, lines : List(Str) }
listed! = |measured, min|
	match Depth.listing(measured, min) {
		Ok(listing) => listing
		Err(why) => {
			Shale.fail!(why)
			{ summary: "", lines: [] }
		}
	}

## `got` equal to `want`, or the self-test stops, naming `what`.
same! : Str, Str, Str => {}
same! = |what, got, want|
	if got != want {
		Shale.fail!("list_deep_nesting self-test: ${what} is\n${got}\nit should be\n${want}")
	}

joined : List(Str) -> Str
joined = |lines| Str.join_with(lines, "\n")
