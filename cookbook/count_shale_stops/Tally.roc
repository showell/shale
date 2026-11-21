## Tally counts texts, each in the order it was first counted, and prints
## the most counted as the Shale original printed them, with Shale's
## `table(rows, {"n", "what"})`: the counts right-aligned under `n`, two
## spaces, the text, and no space at a line's end.
Tally := [].{

	## Counts with none counted yet.
	empty : Dict(Str, U64)
	empty = Dict.empty()

	## The counts with one more of `what`. A new text goes after every text
	## counted before it, as a Shale map's keys do.
	bump : Dict(Str, U64), Str -> Dict(Str, U64)
	bump = |counts, what|
		counts.update(
			what,
			|was|
				match was {
					Ok(count) => Ok(count + 1)
					Err(Missing) => Ok(1)
				},
		)

	## The `most` texts counted most, highest first, ties in the order
	## they were first counted (Shale's `sort` is stable), as a table.
	## `most` under 1 shows the heading alone.
	ranked_table : Dict(Str, U64), I64 -> Str
	ranked_table = |counts, most| {
		shown = if most > 0 {
			most.to_u64_wrap()
		} else {
			0
		}
		rows =
			counts
				.to_list()
				.map_with_index(|(what, count), order| { count, order, what })
				.sort_with(compare_rows)
				.take_first(shown)
		count_cells = rows.map(|row| row.count.to_str())
		width = count_cells.fold(1, |widest, cell| U64.max(widest, cell.count_utf8_bytes()))
		# A column of integers is right-aligned, its heading too; with no
		# rows there are none, and Shale aligns the heading left.
		heading = if rows.is_empty() {
			"n"
		} else {
			right_aligned("n", width)
		}
		lines = rows.map(|row| "${right_aligned(row.count.to_str(), width)}  ${row.what}")
		Str.join_with(lines.prepend("${heading}  what").map(without_trailing_spaces), "\n")
	}
}

## A row of the ranking: its count, where it was first counted, its text.
Row : {
	count : U64,
	order : U64,
	what : Str,
}

compare_rows : Row, Row -> [Before, Same, After]
compare_rows = |left, right|
	if left.count > right.count {
		Before
	} else if left.count < right.count {
		After
	} else if left.order < right.order {
		Before
	} else if left.order > right.order {
		After
	} else {
		Same
	}

right_aligned : Str, U64 -> Str
right_aligned = |cell, width| {
	gap = width - U64.min(width, cell.count_utf8_bytes())
	"${" ".repeat(gap)}${cell}"
}

## The line with the spaces at its end taken off, as Shale's `table` does
## (spaces only: a tab stays).
without_trailing_spaces : Str -> Str
without_trailing_spaces = |line| {
	bytes = line.to_utf8()
	match bytes.find_last_index(|byte| byte != ' ') {
		Ok(last) => Str.from_utf8_lossy(bytes.take_first(last + 1))
		Err(_) => ""
	}
}
