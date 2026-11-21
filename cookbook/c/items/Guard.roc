## A header's guard, framed: Split.items gives a header's items to
## Guard.framed, which makes its `#ifndef` and `#define` one item and its
## `#endif` another.
import Item

Guard := [].{

	## A header's items with its guard framed: its `#ifndef SHALE_<NAME>_H` and
	## the `#define` after it made one item, and the `#endif` that ends the file
	## another, each of kind `Guard`, named for the guard. A header without that
	## framing is an error.
	framed : Str, List(Item) -> Try(List(Item), Str)
	framed = |path, items| {
		name = file_name(path)
		stem = Str.from_utf8_lossy(name.to_utf8().drop_last(2))
		guard = "SHALE_${upper(stem)}_H"
		opening = "ifndef ${guard}"
		var $out = []
		var $is_framed = Bool.False
		var $at = 0
		while $at < items.len() {
			item = items.get($at) ?? Item.new(Directive, "", { first: 0, last: 0 })
			if !($is_framed) and item.kind == Directive and item.name == opening {
				match items.get($at + 1) {
					Ok(define) if define.kind == Macro and define.name == guard => {
						guard_item = { ..Item.new(Guard, guard, { first: item.first, last: define.last }), comment_first: item.comment_first, comment_last: item.comment_last }
						$out = $out.append(guard_item)
						$is_framed = Bool.True
						$at = $at + 2
					}
					_ => {
						why = "a guard's `#ifndef` with no `#define ${guard}` after it"
						return Err(unaccounted(path, item.first, why, "#ifndef ${guard}"))
					}
				}
			} else {
				$out = $out.append(item)
				$at = $at + 1
			}
		}
		if !($is_framed) {
			return Err(unaccounted(path, 1, "a header with no `#ifndef ${guard}`", name))
		}
		match $out.last() {
			Ok(tail) if tail.kind == Directive and tail.name == "endif" =>
				Ok($out.drop_last(1).append({ ..tail, kind: Guard, name: guard }))
			Ok(tail) => Err(unaccounted(path, tail.first, "a header whose last item is not its guard's `#endif`", tail.name))
			Err(_) => Err(unaccounted(path, 1, "a header with no `#ifndef ${guard}`", name))
		}
	}
}

## The path's last part, after its last `/`.
file_name : Str -> Str
file_name = |path| {
	bytes = path.to_utf8()
	match bytes.find_last_index(|byte| byte == '/') {
		Ok(slash) => Str.from_utf8_lossy(bytes.drop_first(slash + 1))
		Err(_) => path
	}
}

## The text with its ASCII letters made capitals, as Shale's `upper` does.
upper : Str -> Str
upper = |text|
	Str.from_utf8_lossy(
		text.to_utf8().map(
			|byte|
				if byte >= 'a' and byte <= 'z' {
					byte - 32
				} else {
					byte
				},
		),
	)

## The error for line `number` of the file at `path`: its place and why
## first, then what the line shows.
unaccounted : Str, U64, Str, Str -> Str
unaccounted = |path, number, why, shown|
	"${path}:${number.to_str()}: ${why}: ${shown}"
