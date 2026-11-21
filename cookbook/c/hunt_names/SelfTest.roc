## hunt_names' self-test: the hunt run over cookbook/c/plants/hunt_names/,
## each plant named for the check it holds (`do_word.c`), each near miss in
## `quiet_<check>.c`, and the findings held to the plants' list exactly:
## every check fires on its own plant and on no other, and a quiet plant on
## none.
import pf.Shale
import Checks

SelfTest := [].{

	## Every check, a FAIL line for each that fails and then a stop, else the
	## PASS line. `hunt!` is the recipe's own, a folder's findings, sorted.
	run! : (Str => { found : List(Checks.Finding), files : U64 }) => {}
	run! = |hunt!| {
		found = hunt!(plants).found
		fired = found.map(|finding| (finding.path, finding.check))
		silent =
			Checks.check_names
				.keep_if(|check| !(fired.contains((plant_of(check), check))))
				.map(|check| "`${check}` does not fire on its plant ${plant_of(check)}")
		elsewhere =
			found
				.keep_if(|finding| finding.path != plant_of(finding.check))
				.map(|finding| "`${finding.check}` fires on ${finding.path}:${finding.line.to_str()}, not its plant")
		got = Str.join_with(found.map(Checks.finding_line), "\n")
		want = Str.join_with(wanted, "\n")
		listed = if got == want {
			[]
		} else {
			["the plants' findings are\n${got}\nthey should be\n${want}"]
		}
		wrong = silent.concat(elsewhere).concat(listed)
		for why in wrong {
			Shale.print!("FAIL ${why}")
		}
		if !(wrong.is_empty()) {
			Shale.fail!("hunt_names self-test: ${wrong.len().to_str()} wrong")
		}
		checks = "${Checks.check_names.len().to_str()} checks, each firing on its own plant alone"
		Shale.print!("PASS hunt_names self-test: ${checks}, ${wanted.len().to_str()} findings exactly, the quiet plants on none")
	}
}

## Where the plants are.
plants : Str
plants = "cookbook/c/plants/hunt_names"

## The plant a check fires on.
plant_of : Str -> Str
plant_of = |check| "${plants}/${check}.c"

## The plants' findings, every one, in order.
wanted : List(Str)
wanted = {
	p = "${plants}/"
	[
		"${p}abbreviation.c:4: abbreviation: `bi` for `builtin`, which other names spell out, in 1 name here: `bi_length`",
		"${p}bound_order.c:4: bound_order: `BOUND_MAX_NAME` has `MAX` before its last word: a bound ends with it, as `BOUND_NAME_MAX` would",
		"${p}constant_case.c:4: constant_case: the constant `constant_wrong` is not UPPER_SNAKE",
		"${p}container_type.c:4: container_type: the type `Entry` is only container words: name it for what it holds",
		"${p}container_type.c:8: container_type: the type `NodeInfo` is only container words: name it for what it holds",
		"${p}do_word.c:4: do_word: `store_do_write` has `do` in it, which says only that it acts: name the act",
		"${p}function_case.c:4: function_case: the function `readThing` is not snake_case",
		"${p}type_case.c:4: type_case: the type `bad_type` is not PascalCase",
		"${p}vague.c:4: vague: `handle_data` is only vague words: what does it act on, and how?",
		"${p}vague_local.c:4: vague_local: `vague_local_sum`'s parameter `data` says nothing of what it holds",
		"${p}vague_local.c:5: vague_local: `vague_local_sum`'s local `tmp` says nothing of what it holds",
		"${p}zig_bound.c:4: zig_bound: `ZIG_BOUND_MOST` is a bound in Zig's words: C's are `_MAX` and `_MIN`",
		"${p}zig_bound.c:8: zig_bound: `ZIG_BOUND_LEAST` is a bound in Zig's words: C's are `_MAX` and `_MIN`",
	]
}
