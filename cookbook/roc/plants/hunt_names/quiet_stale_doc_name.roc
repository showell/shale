# Quiet for every check: docs that name what is here, beside it or past
# what can be judged.
import QuietDocShelf

## As wide as `QuietDocShelf.shelf_width`, or `shelf_width` bare.
## The `best_at` found by a `for` or a `fold`, and a `stop_count` field.
## Like `List.map(xs, f)` and `Str.join_with`, whose modules are no file here.
width : U64
width = {
	best_at = 3
	record = { stop_count: best_at }
	record.stop_count + QuietDocShelf.shelf_width
}

## Its own effect, `read_shelf!`, with or without the `!`: `read_shelf`.
read_shelf! : U64 => U64
