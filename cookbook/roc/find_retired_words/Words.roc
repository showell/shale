## A project's decided words, read from its words file: each line
## `kept: retired, retired`, in lower case, such as `lean: tilt`, a blank
## line or one starting with `#` skipped. A missing file, a malformed line, a
## word retired twice or a word both kept and retired stops the run, as the
## Shale original did, on the same line and in the same words.
import pf.Shale
import lib.Text

Words := [].{

	## Each retired word, with the word kept for it.
	Decided : Dict(Str, Str)

	## The words file at `path`, read and checked.
	read! : Str => Words.Decided
	read! = |path| {
		if !(Shale.exists!(path)) {
			Shale.fail!("find_retired_words: no words file ${path}")
		}
		text = match Shale.read!(path) {
			Found(found) => found
			Missing => {
				Shale.fail!("find_retired_words: no words file ${path}")
				""
			}
		}
		var $decided = Dict.empty()
		var $retired_in_order = []
		var $kept = Set.empty()
		var $number = 0.U64
		for raw_line in Text.lines(text) {
			$number = $number + 1
			line = Text.trim(raw_line)
			if line != "" and !(line.starts_with("#")) {
				place = "${path}:${$number.to_str()}"
				entry = read_entry!(line, place)
				$kept = $kept.insert(entry.kept)
				for word in entry.retired {
					if $decided.contains(word) {
						Shale.fail!("find_retired_words: ${place}: `${word}` is retired twice")
					}
					$decided = $decided.insert(word, entry.kept)
					$retired_in_order = $retired_in_order.append(word)
				}
			}
		}
		for word in $retired_in_order {
			if $kept.contains(word) {
				Shale.fail!("find_retired_words: ${path}: `${word}` is both kept and retired")
			}
		}
		$decided
	}

	## Each retired word in `text`, once, sorted bytewise: the words of each
	## name in it (a run of letters, digits and underscores) looked up.
	retired_in : List(U8), Words.Decided -> List(Str)
	retired_in = |text, decided| {
		var $found = []
		for run in name_runs(text) {
			for word in Text.name_words(Str.from_utf8_lossy(run)) {
				if decided.contains(word) and !($found.contains(word)) {
					$found = $found.append(word)
				}
			}
		}
		$found.sort_with(Text.byte_order)
	}

	## The word kept for a retired one.
	kept_for : Words.Decided, Str -> Str
	kept_for = |decided, retired|
		match decided.get(retired) {
			Ok(kept) => kept
			Err(_) => crash "find_retired_words: `${retired}` is not retired"
		}
}

## One line of the words file, trimmed: the kept word and the words it
## retires. A line of another shape stops the run, naming its place.
read_entry! : Str, Str => { kept : Str, retired : List(Str) }
read_entry! = |line, place|
	match Shale.re_find!(line, entry_pattern) {
		Found(found) =>
			match found.groups {
				[kept, retired] => { kept, retired: retired.split_on(",").map(Text.trim) }
				_ => crash "find_retired_words: the entry pattern has two groups"
			}
		NoMatch => {
			Shale.fail!("find_retired_words: ${place}: expected `kept: retired, retired`, in lower case")
			{ kept: "", retired: [] }
		}
	}

## A words file's line: the kept word, a colon, and the retired words
## separated by commas.
entry_pattern : Str
entry_pattern = "^([a-z0-9]+): *([a-z0-9]+(?:, *[a-z0-9]+)*)$"

## The runs of letters, digits and underscores in the bytes, left to right
## (the Shale original's `[A-Za-z0-9_]+`, every match).
name_runs : List(U8) -> List(List(U8))
name_runs = |bytes| {
	var $runs = []
	var $run = []
	for byte in bytes {
		if is_name_byte(byte) {
			$run = $run.append(byte)
		} else if !($run.is_empty()) {
			$runs = $runs.append($run)
			$run = []
		}
	}
	if $run.is_empty() {
		$runs
	} else {
		$runs.append($run)
	}
}

is_name_byte : U8 -> Bool
is_name_byte = |byte|
	Text.is_capital(byte) or Text.is_small_letter(byte) or Text.is_digit(byte) or byte == '_'
