## find_dead_code's self-test: the recipe run over
## cookbook/c/plants/find_dead_code/ and held to the plants' own list of
## findings, exactly: each way to be used (a call, a pointer in a table, a
## macro, a generator's function or its type) and each near miss (a comment,
## a string, a member, a prototype, recursion, a macro nobody uses, a
## same-named helper in another test file).
import pf.Shale
import Graph

SelfTest := [].{

	## The check, a stop when it fails, then the PASS line. `find!` is the
	## recipe's own, the findings over a folder.
	run! : (Str => { findings : List(Graph.Finding), nodes : U64 }) => {}
	run! = |find!| {
		got = joined(find!(plants).findings.map(Graph.finding_line))
		want = [
			"lib.c:6: dead: Chars (a VEC_DEFINE generator)",
			"lib.c:18: dead: lib_proto_only",
			"lib.c:43: dead: lib_in_unused_macro (named only by unreached code: LIB_UNUSED_CALL)",
			"lib.c:50: dead: lib_recursive",
			"lib.c:56: dead: lib_by_dead (named only by unreached code: lib_recursive)",
			"lib.c:61: dead: lib_in_comment",
			"lib.c:66: dead: lib_member",
			"lib.c:71: test_only: lib_tested",
			"lib.h:11: dead: lib_header_dead",
			"lib.h:14: test_only: lib_header_tested",
			"test.h:14: dead: test_unused_kit",
			"tests/test_lib.c:10: dead: test_lib_stray",
			"tests/test_lib.c:16: dead: pick",
		]
		wanted = joined(want.map(|line| "${plants}/${line}"))
		if got != wanted {
			Shale.fail!("find_dead_code self-test: the plants' findings are\n${got}\nthey should be\n${wanted}")
		}
		Shale.print!("PASS find_dead_code self-test: ${want.len().to_str()} findings over the plants, exactly, and each near miss left alone")
	}
}

## Where the plants are.
plants : Str
plants = "cookbook/c/plants/find_dead_code"

joined : List(Str) -> Str
joined = |lines| Str.join_with(lines, "\n")
