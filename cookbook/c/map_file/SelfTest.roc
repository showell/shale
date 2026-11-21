## map_file's self-test: the splitter held to the plants in
## cookbook/c/plants/map_file/, a header and a .c file holding an item of
## each kind, each mapped and held to its map, its comments, heads, bodies
## and uses; then each plant in breaks/ split, which must stop naming its
## file and the line it breaks. The masker is held first, to two lines.
import pf.Shale
import items.Item
import items.Mask
import items.Split

SelfTest := [].{

	## Every check, each a stop when it fails, then the PASS line.
	run! : {} => {}
	run! = |{}| {
		masker_checks = check_masker!({})
		header_checks = check_header!({})
		source_checks = check_source!({})
		break_checks = check_breaks!({})
		checks = masker_checks + header_checks + source_checks + break_checks
		kinds = "each item kind mapped and ${break_checks.to_str()} shape breaks stopped at their lines"
		Shale.print!("PASS map_file self-test: ${checks.to_str()} checks, ${kinds}")
	}
}

## Where the plants are.
plants : Str
plants = "cookbook/c/plants/map_file"

## `got` equal to `want`, or the self-test stops, naming `what`.
same! : Str, Str, Str => {}
same! = |what, got, want|
	if got != want {
		Shale.fail!("map_file self-test: ${what} is\n${got}\nit should be\n${want}")
	}

## The masker: every column kept, a `"` in a character literal the
## literal's, a `//` in a string the string's, a macro's `/* */` blanked.
check_masker! : {} => U64
check_masker! = |{}| {
	raw = "    if (c == '\"') { x(\"a // b\"); } // c"
	same!("the mask of `${raw}`", mask(raw), "    if (c == ' ') { x(\"      \"); }     ")
	macro_line = "#define M(x) /* it's */ \\"
	same!("the mask of `${macro_line}`", mask(macro_line), "#define M(x) ${" ".repeat(10)} \\")
	2
}

mask : Str -> Str
mask = |line| Mask.text(Mask.masked(line.to_utf8()))

## The plant's items, with their uses, or the self-test stopped.
split_plant! : Str => List(Item)
split_plant! = |path| {
	text = match Shale.read!(path) {
		Found(found) => found
		Missing => {
			Shale.fail!("map_file self-test: ${path} is not there")
			""
		}
	}
	match Split.items({ path, text, uses: Bool.True }) {
		Ok(items) => items
		Err(why) => {
			Shale.fail!(why)
			[]
		}
	}
}

## The map of the items, one `first-last kind name` a line.
map_text : List(Item) -> Str
map_text = |items| {
	map_lines = items.map(|item| "${item.first.to_str()}-${item.last.to_str()} ${Item.kind_name(item.kind)} ${item.name}")
	Str.join_with(map_lines, "\n")
}

## The item of the kind and name, or the self-test stopped.
item_named! : List(Item), Item.Kind, Str => Item
item_named! = |items, kind, name|
	match items.find_first(|item| item.kind == kind and item.name == name) {
		Ok(item) => item
		Err(_) => {
			Shale.fail!("map_file self-test: no ${Item.kind_name(kind)} `${name}`")
			Item.new(kind, name, { first: 0, last: 0 })
		}
	}

## An item's lines as `comment a-b, sig c-d, body e-f`.
spans : Item -> Str
spans = |item| {
	comment = "comment ${item.comment_first.to_str()}-${item.comment_last.to_str()}"
	sig = "sig ${item.sig_first.to_str()}-${item.sig_last.to_str()}"
	"${comment}, ${sig}, body ${item.body_first.to_str()}-${item.body_last.to_str()}"
}

joined : List(Str) -> Str
joined = |names| Str.join_with(names, ",")

## kinds.h: its map, then its items' spans, macros, enumerators and uses.
check_header! : {} => U64
check_header! = |{}| {
	path = "${plants}/kinds.h"
	items = split_plant!(path)
	map = [
		"2-3 guard SHALE_KINDS_H",
		"5-5 include stdint.h",
		"6-6 include slice.h",
		"9-9 macro KINDS_MAX",
		"10-17 macro KINDS_DEFINE",
		"19-19 directive if defined",
		"20-20 macro KINDS_LINUX",
		"21-21 directive endif",
		"23-26 struct KindsPair",
		"28-28 enum KindsKind",
		"30-33 union KindsWord",
		"35-35 typedef KindsHook",
		"37-40 enum KINDS_FIRST",
		"42-42 generator KindsPairs",
		"43-43 generator KindsVec",
		"45-45 static_assert KindsPair",
		"47-47 function kinds_sum",
		"51-58 function kinds_name",
		"60-61 prototype kinds_check",
		"63-66 data KINDS_NAMES",
		"67-67 data KINDS_ONE",
		"69-69 guard SHALE_KINDS_H",
	]
	same!("${path}'s map", map_text(items), Str.join_with(map, "\n"))
	defined = item_named!(items, Macro, "KINDS_DEFINE")
	same!("KINDS_DEFINE's makes", joined(defined.makes), "make,first")
	same!("KINDS_DEFINE's spans", spans(defined), "comment 0-0, sig 10-10, body 11-17")
	same!("KINDS_MAX's spans", spans(item_named!(items, Macro, "KINDS_MAX")), "comment 8-8, sig 9-9, body 0-0")
	same!("KindsKind's enumerators", joined(item_named!(items, Enum, "KindsKind").enumerators), "KINDS_A,KINDS_B")
	anonymous = item_named!(items, Enum, "KINDS_FIRST")
	same!("the anonymous enum's enumerators", joined(anonymous.enumerators), "KINDS_FIRST,KINDS_SECOND")
	vec = item_named!(items, Generator, "KindsVec")
	same!("KindsVec's macro and type", "${vec.macro} ${vec.held_type}", "VEC_DEFINE KindsPair")
	sum = item_named!(items, Function, "kinds_sum")
	sum_said = "${sum.storage} | ${sum.return_type} | ${spans(sum)} | ${joined(sum.uses)}"
	same!("kinds_sum", sum_said, "static inline | uint32_t | comment 0-0, sig 47-47, body 47-47 | p")
	named = item_named!(items, Function, "kinds_name")
	named_said = "${named.return_type} | ${spans(named)} | ${joined(named.uses)}"
	same!("kinds_name", named_said, "Str | comment 49-50, sig 51-51, body 52-57 | KINDS_A,KINDS_LINUX,S,k,kinds_label")
	check = item_named!(items, Prototype, "kinds_check")
	same!("kinds_check", "${check.return_type} | ${spans(check)}", "Status | comment 0-0, sig 60-61, body 0-0")
	same!("KINDS_NAMES's spans", spans(item_named!(items, Data, "KINDS_NAMES")), "comment 0-0, sig 63-63, body 64-65")
	12
}

## kinds.c: its map, then its function's, test's and test list's uses.
check_source! : {} => U64
check_source! = |{}| {
	path = "${plants}/kinds.c"
	items = split_plant!(path)
	map = [
		"2-2 include test.h",
		"4-4 data kinds_hook",
		"6-11 function kinds_wrapped",
		"13-15 test kinds_test",
		"17-19 test_list TEST_MAIN",
		"21-23 function main",
	]
	same!("${path}'s map", map_text(items), Str.join_with(map, "\n"))
	wrapped = item_named!(items, Function, "kinds_wrapped")
	same!("kinds_wrapped", "${spans(wrapped)} | ${joined(wrapped.uses)}", "comment 0-0, sig 6-7, body 8-10 | a,b,kinds_add,other,p,sum")
	same!("kinds_test's uses", joined(item_named!(items, Test, "kinds_test").uses), "EXPECT,kinds_wrapped")
	same!("TEST_MAIN's uses", joined(item_named!(items, TestList, "TEST_MAIN").uses), "TEST_MAIN,kinds_test")
	same!("main's spans", spans(item_named!(items, Function, "main")), "comment 0-0, sig 21-21, body 22-22")
	5
}

## Each break stops, naming its file, its line and why.
check_breaks! : {} => U64
check_breaks! = |{}| {
	breaks = [
		("indented.c", "indented.c:3: an indented line outside every item"),
		("column0.c", "column0.c:2: a line at column 0 that opens no item the dialect knows"),
		("in_body.c", "in_body.c:3: a line at column 0 inside the function `f` opened at line 2, which only a `}` may close"),
		("unclosed.c", "unclosed.c:2: the function `f` that no `}` at column 0 closes"),
		("no_guard.h", "no_guard.h:1: a header with no `#ifndef SHALE_NO_GUARD_H`"),
		("no_endif.h", "no_endif.h:5: a header whose last item is not its guard's `#endif`"),
		("bad_test.c", "bad_test.c:2: a `TEST(` line that is not `TEST(name) {`"),
		("wrapped_head.c", "wrapped_head.c:3: a line at column 0 inside the head opened at line 2"),
		("bad_generator.c", "bad_generator.c:2: a generator line whose name and type this reader can't find"),
	]
	for (file, said) in breaks {
		path = "${plants}/breaks/${file}"
		wanted = "${plants}/breaks/${said}: "
		text = match Shale.read!(path) {
			Found(found) => found
			Missing => ""
		}
		match Split.items({ path, text, uses: Bool.False }) {
			Ok(items) => Shale.fail!("map_file self-test: ${path} split, with ${items.len().to_str()} items; it should stop with ${said}")
			Err(why) =>
				if !(why.starts_with(wanted)) {
					Shale.fail!("map_file self-test: ${path} stopped with `${why}`; it should stop with `${wanted}<the line>`")
				}
		}
	}
	breaks.len()
}
