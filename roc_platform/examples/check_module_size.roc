## cookbook/roc/check_module_size.shale, ported to Roc on Shale's platform:
## each Roc module held to a size, 500 lines at most, the whole file
## counted. Its header says the rules; this port keeps its arguments, its
## output and its stops, and leaves out `self_test` (the original's plants
## stay its own).
##
##   shale roc_platform/run.shale script=roc_platform/examples/check_module_size.roc glob=app/**/*.roc
##
## The arguments: `glob` (commas between several, `**/*.roc` by default),
## `tests` (`*Spec.roc,test/`), `exempt` (an exemptions file, `path --
## reason` a line), `limit` (500) and `all` (`all=1` lists every module).
## budget: steps=50000000 reads=100000
app [main!] { pf: platform "../platform/main.roc" }

import pf.Shale

main! : {} => {}
main! = |{}| {
	limit_text = Shale.arg!("limit", "500")
	limit = match U64.from_str(limit_text) {
		Ok(n) => n
		Err(_) => {
			Shale.fail!("check_module_size: limit=${limit_text} is no number")
			0
		}
	}
	opts = {
		glob: Shale.arg!("glob", "**/*.roc"),
		tests: Shale.arg!("tests", "*Spec.roc,test/"),
		exempt: Shale.arg!("exempt", ""),
		limit,
	}
	all = Shale.arg!("all", "") == "1"
	r = module_sizes!(opts)
	if all {
		for l in r.listing {
			Shale.print!(l)
		}
	}
	print_findings!(r.summary, r.found)
	if !(List.is_empty(r.found)) {
		Shale.fail!(r.summary)
	}
}

## The summary, each finding, then the summary again (cookbook/findings.shale).
print_findings! : Str, List(Str) => {}
print_findings! = |summary, found| {
	Shale.print!(summary)
	if !(List.is_empty(found)) {
		for l in found {
			Shale.print!(l)
		}
		Shale.print!(summary)
	}
}

comma_items : Str -> List(Str)
comma_items = |s|
	Str.split_on(s, ",")
		.map(Str.trim)
		.keep_if(|x| !(Str.is_empty(x)))

## Whether `path` is in one of the test roots: a folder anywhere in it (`x/`),
## a file name's ending (`*X`), or a whole file name.
in_test_root : Str, List(Str) -> Bool
in_test_root = |path, roots| {
	base = basename(path)
	List.any(
		roots,
		|r|
			if Str.ends_with(r, "/") {
				Str.starts_with(path, r) or Str.contains(path, "/${r}") or Str.contains(path, ":${r}")
			} else if Str.starts_with(r, "*") {
				Str.ends_with(base, Str.drop_prefix(r, "*"))
			} else {
				base == r
			},
	)
}

basename : Str -> Str
basename = |path|
	match List.last(Str.split_on(path, "/")) {
		Ok(b) => b
		Err(_) => path
	}

Exemption : { path : Str, reason : Str }

## The exemptions, each `path -- reason`; a malformed line, a missing path or
## one given twice stops the run.
exemptions_of! : Str => List(Exemption)
exemptions_of! = |file| {
	if file == "" {
		return []
	}
	text = match Shale.read!(file) {
		Found(t) => t
		Missing => {
			Shale.fail!("check_module_size: no exemptions file ${file}")
			""
		}
	}
	var $out = []
	var $number = 0
	for raw in lines(text) {
		$number = $number + 1
		l = Str.trim(raw)
		if l != "" and !(Str.starts_with(l, "#")) {
			place = "${file}:${U64.to_str($number)}: "
			match Str.split_first(l, " -- ") {
				Ok({ before, after }) => {
					reason = Str.trim(after)
					path = Str.trim(before)
					if reason == "" {
						Shale.fail!("check_module_size: ${place}expected `path -- reason`, got `${l}`")
					}
					if !(Shale.exists!(path)) {
						Shale.fail!("check_module_size: ${place}`${path}` is no file")
					}
					if List.any($out, |e| e.path == path) {
						Shale.fail!("check_module_size: ${place}`${path}` is exempted twice")
					}
					$out = $out.append({ path, reason })
				}
				Err(_) => Shale.fail!("check_module_size: ${place}expected `path -- reason`, got `${l}`")
			}
		}
	}
	$out
}

## A text's lines, as Shale's `lines` splits it: a last newline ends a line,
## it does not begin one.
lines : Str -> List(Str)
lines = |text| {
	parts = Str.split_on(text, "\n")
	if Str.ends_with(text, "\n") {
		List.sublist(parts, { start: 0, len: List.len(parts) - 1 })
	} else {
		parts
	}
}

## A file's lines as Shale's `count_lines` counts them: the newlines, and a
## last line that has none.
count_lines : Str -> U64
count_lines = |text| {
	bytes = Str.to_utf8(text)
	newlines = List.count_if(bytes, |b| b == 10)
	match List.last(bytes) {
		Ok(10) => newlines
		Ok(_) => newlines + 1
		Err(_) => 0
	}
}

## Bytewise order, as Shale's `sort` puts strings.
byte_order : Str, Str -> [Before, Same, After]
byte_order = |a, b| {
	x = Str.to_utf8(a)
	y = Str.to_utf8(b)
	var $k = 0
	var $answer = Same
	var $going = Bool.True
	while $going {
		match (List.get(x, $k), List.get(y, $k)) {
			(Ok(p), Ok(q)) =>
				if p < q {
					$answer = Before
					$going = Bool.False
				} else if p > q {
					$answer = After
					$going = Bool.False
				} else {
					$k = $k + 1
				}
			(Err(_), Ok(_)) => {
				$answer = Before
				$going = Bool.False
			}
			(Ok(_), Err(_)) => {
				$answer = After
				$going = Bool.False
			}
			(Err(_), Err(_)) => {
				$going = Bool.False
			}
		}
	}
	$answer
}

## The paths the globs name, each once, sorted, build output left out.
module_paths! : Str => List(Str)
module_paths! = |globs| {
	var $seen = []
	for g in comma_items(globs) {
		for p in Shale.glob!(g) {
			if !(Str.contains(p, "zig-out/")) and !(Str.contains(p, "node_modules/")) and !(List.contains($seen, p)) {
				$seen = $seen.append(p)
			}
		}
	}
	List.sort_with($seen, byte_order)
}

module_sizes! : { glob : Str, tests : Str, exempt : Str, limit : U64 } => { summary : Str, found : List(Str), listing : List(Str) }
module_sizes! = |opts| {
	exemptions = exemptions_of!(opts.exempt)
	roots = comma_items(opts.tests)
	limit = opts.limit
	limit_s = U64.to_str(limit)
	var $found = []
	var $listing = []
	var $modules = 0
	var $tests = 0
	var $over = 0
	var $exempt = 0
	var $unneeded = 0
	for path in module_paths!(opts.glob) {
		n = match Shale.read!(path) {
			Found(text) => count_lines(text)
			Missing => 0
		}
		n_s = U64.to_str(n)
		if in_test_root(path, roots) {
			$tests = $tests + 1
			$listing = $listing.append("${path}: ${n_s} lines, a test root's, no limit")
		} else {
			$modules = $modules + 1
			var $note = ""
			match List.find_first(exemptions, |e| e.path == path) {
				Ok(e) => {
					$exempt = $exempt + 1
					$note = " (exempt: ${e.reason})"
					if n <= limit {
						$unneeded = $unneeded + 1
						$found = $found.append("${path}: unneeded_exemption: ${n_s} lines (limit ${limit_s}) needs no exemption")
					}
				}
				Err(_) =>
					if n > limit {
						$over = $over + 1
						$found = $found.append("${path}: module_size: ${n_s} lines (limit ${limit_s})")
					}
			}
			$listing = $listing.append("${path}: ${n_s} lines${$note}")
		}
	}
	summary = "check_module_size (Roc): ${U64.to_str($modules)} modules, ${U64.to_str($tests)} test files left out; module_size ${U64.to_str($over)} over ${limit_s} lines, ${U64.to_str($exempt)} exempt, unneeded_exemption ${U64.to_str($unneeded)}"
	{ summary, found: $found, listing: $listing }
}
