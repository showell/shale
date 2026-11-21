## list_long_files lists the `.zig` files over a thousand lines in the repos
## we work in, longest first, then how many of all their `.zig` files that
## is. Its launcher, cookbook/zig/list_long_files.shale, is how it is run,
## from the folder that holds the repos:
##
##   cd ~/showell_repos && shale cookbook:zig/list_long_files.shale over=500
##
## The work is plain Roc over three of Shale's effects: `Shale.glob!` names a
## repo's `.zig` files, `Shale.read!` gives each one's text, and
## `Shale.print!` writes the table. `Shale.arg!` reads `over`, and
## `Shale.fail!` stops the run on an `over` that is no integer.
## budget: steps=200000000 reads=100000 read_bytes=1000000000 arena_bytes=1000000000
app [main!] { pf: platform "../../../roc_platform/platform/main.roc" }

import pf.Shale

## The repos it walks, each a folder of the root it runs in. A repo that
## isn't there adds nothing.
repos : List(Str)
repos = ["shale", "angry-gopher", "zig-ast"]

## A `.zig` file and its lines.
ZigFile : { lines : U64, path : Str }

main! : {} => {}
main! = |{}| {
	over = read_limit!({})
	var $files = []
	for repo in repos {
		for path in Shale.glob!("${repo}/**/*.zig") {
			$files = $files.append({ lines: line_count(read_text!(path)), path })
		}
	}
	long_files = $files.keep_if(|file| file.lines.to_i64_wrap() > over)
	longest_first = long_files.sort_by_reversed(|file| file.lines)
	Shale.print!(long_files_table(longest_first))
	counts = "${long_files.len().to_str()} of ${$files.len().to_str()}"
	Shale.print!("${counts} .zig files are over ${over.to_str()} lines")
}

## The `over=` limit: 1000 unless given, and any integer, so `over=-1`
## lists every file.
read_limit! : {} => I64
read_limit! = |{}| {
	given = Shale.arg!("over", "1000")
	match I64.from_str(given) {
		Ok(limit) => limit
		Err(_) => {
			Shale.fail!("list_long_files: over=${given} is not an integer")
			0
		}
	}
}

## The file's text. A file the glob named and the read finds gone stops the
## run.
read_text! : Str => Str
read_text! = |path|
	match Shale.read!(path) {
		Found(text) => text
		Missing => {
			Shale.fail!("list_long_files: ${path} went missing during the walk")
			""
		}
	}

## The newlines, and a last line that has none, as Shale's `count_lines`
## counts them.
line_count : Str -> U64
line_count = |text| {
	bytes = text.to_utf8()
	newlines = bytes.count_if(|byte| byte == '\n')
	match bytes.last() {
		Ok('\n') => newlines
		Ok(_) => newlines + 1
		Err(_) => 0
	}
}

## The files as Shale's `table` prints them: a line of headings, then a line
## a file, the line counts right-aligned under `lines` and the paths after
## two spaces.
long_files_table : List(ZigFile) -> Str
long_files_table = |files| {
	width = files.fold(
		"lines".count_utf8_bytes(),
		|widest, file| U64.max(widest, file.lines.to_str().count_utf8_bytes()),
	)
	heading = "${right_aligned("lines", width)}  path"
	rows = files.map(|file| "${right_aligned(file.lines.to_str(), width)}  ${file.path}")
	Str.join_with(rows.prepend(heading), "\n")
}

right_aligned : Str, U64 -> Str
right_aligned = |cell, width|
	"${" ".repeat(width - cell.count_utf8_bytes())}${cell}"
