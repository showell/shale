## The lines a rewrite leaves wider than the width. A comment, a line wide
## only for the text of its strings (by Roc's parser, as check_stacking's
## `wide string`) and a line of numeric data, which stays packed, are
## counted, as no trailing comma shortens one; any other is listed, as
## `path:line (width): nothing to stack: text`. Only a file with a wide line
## is parsed, for its strings.
import pf.Shale
import lib.Bytes
import lib.Stacking
import lib.Text
import Rewrite

WideLines := [].{

	## The lines left wide so far: those listed, the counts of the rest, and
	## why each file roc-ast refused was refused.
	Report : {
		long : List(Str),
		wide_strings : U64,
		data : U64,
		comments : U64,
		refused : List(Str),
	}

	## A report with nothing in it yet.
	fresh : WideLines.Report
	fresh = { long: [], wide_strings: 0, data: 0, comments: 0, refused: [] }

	## The report with the lines wider than `width` in the files `paths` of
	## folder `at` added, each named as folder `dir`'s.
	report_wide_lines! : WideLines.Report,
	{
		dir : Str,
		at : Str,
		paths : List(Str),
		width : U64,
	} => WideLines.Report
	report_wide_lines! = |before, given| {
		width = given.width
		is_wide = |text| Bytes.line_width(text) > width
		var $wide = []
		for path in given.paths {
			at_path = "${given.at}/${path}"
			if Rewrite.read_text!(at_path).split_on("\n").any(is_wide) {
				$wide = $wide.append(at_path)
			}
		}
		wide = $wide
		if wide.is_empty() {
			return before
		}
		var $report = before
		var $parsed = []
		for file in Shale.roc_tree!(wide) {
			if file.err == "" {
				$parsed = $parsed.append(file)
			} else if file.err.starts_with(file.path) {
				$report = { ..$report, refused: $report.refused.append(file.err) }
			} else {
				$report = { ..$report, refused: $report.refused.append("${file.path}: ${file.err}") }
			}
		}
		cut = given.at.count_utf8_bytes()
		for file in $parsed {
			source_lines = Rewrite.read_text!(file.path).split_on("\n")
			strings = match Stacking.string_columns_on_lines(file.nodes, source_lines) {
				Ok(by_line) => by_line
				Err(CrossesLine(line)) => {
					Shale.fail!("wrap_long_lines: ${file.path}: line ${line.to_str()}: a run of string text crosses a line")
					Dict.empty()
				}
			}
			named = "${given.dir}${Text.bytes_of(file.path, { start: cut, count: file.path.count_utf8_bytes() })}"
			$number : U64
			var $number = 0
			for text in source_lines {
				$number = $number + 1
				line_width = Bytes.line_width(text)
				if line_width > width {
					$report = judged_line(
						$report,
						{
							text,
							line_width,
							string_width: strings.get($number) ?? 0,
							place: "${named}:${$number.to_str()}",
							width,
						},
					)
				}
			}
		}
		$report
	}

	## The report's counts, as its summary line says them.
	summary : WideLines.Report, U64 -> Str
	summary = |report, width| {
		listed = "${report.long.len().to_str()} code lines with nothing to stack"
		counted = "${report.wide_strings.to_str()} wide only for their strings, ${report.data.to_str()} of numeric data, ${report.comments.to_str()} comment lines"
		"still over ${width.to_str()}: ${listed}, ${counted}"
	}
}

## The report with one line wider than the width counted or listed.
judged_line : WideLines.Report,
{
	text : Str,
	line_width : U64,
	string_width : U64,
	place : Str,
	width : U64,
} -> WideLines.Report
judged_line = |report, line|
	if Rewrite.is_comment(line.text) {
		{ ..report, comments: report.comments + 1 }
	} else if line.line_width - line.string_width <= line.width {
		{ ..report, wide_strings: report.wide_strings + 1 }
	} else if Stacking.is_data_line(line.text, line.width) {
		{ ..report, data: report.data + 1 }
	} else {
		shown = Str.from_utf8_lossy(Bytes.trimmed(Bytes.span(line.text.to_utf8(), 1, 140)))
		listed = "${line.place} (${line.line_width.to_str()}): nothing to stack: ${shown}"
		{ ..report, long: report.long.append(listed) }
	}
