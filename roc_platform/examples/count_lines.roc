## The tiny example: a file's lines counted, as Shale's `count_lines` counts
## them (a last line without its newline counts).
##
##   shale roc_platform/run.shale script=roc_platform/examples/count_lines.roc file=README.md
app [main!] { pf: platform "../platform/main.roc" }

import pf.Shale

main! : {} => {}
main! = |{}| {
	file = Shale.arg!("file", "README.md")
	match Shale.read!(file) {
		Found(text) => Shale.print!("${file}: ${U64.to_str(count_lines(text))} lines")
		Missing => Shale.print!("${file}: no such file")
	}
}

## The newlines, and a last line that has none.
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
