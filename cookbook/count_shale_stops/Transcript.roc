## Transcript reads what count_shale_stops needs from one Claude Code
## transcript line, read by ShaleJson: its message's parts, each a call of
## the Bash tool, an answer to a call, or neither, and from an answer its
## text and whether it was an error.
##
## Each field is read where the Shale original read it and no sooner, so a
## line it stopped on stops here and a line it passed passes: a field that
## isn't there, or is not what the original's next step takes, is an `Err`
## saying which, in the words of the Shale stop it stands for.
import ShaleJson

Transcript := [].{

	## The parts of the line's `message.content`, or none when the line is
	## older than `since` or its content is not a list. `since` is `""` for
	## every line, else an ISO time the line's `timestamp` is held to as
	## text.
	recent_parts : ShaleJson, Str -> Try(List(ShaleJson), Str)
	recent_parts = |line, since| {
		is_recent =
			if since == "" {
				True
			} else {
				at_least(timestamp(line)?, since)
			}
		message = field(line, "message")?
		content = field(message, "content")?
		if is_recent and is_list(content) {
			match content {
				Array(parts) => Ok(parts)
				_ => Ok([])
			}
		} else {
			Ok([])
		}
	}

	## What a part of a message is: a call of the Bash tool (its command),
	## an answer to a call (the call's id), or neither.
	part_kind : ShaleJson -> Try([ShellCall(Str), Answer(Str), Neither], Str)
	part_kind = |part| {
		kind = field(part, "type")?
		is_shell_call =
			if is_text(kind, "tool_use") {
				is_text(field(part, "name")?, "Bash")
			} else {
				False
			}
		if is_shell_call {
			input = field(part, "input")?
			command = text_of(field(input, "command")?, "the command")?
			Ok(ShellCall(command))
		} else if is_text(kind, "tool_result") {
			Ok(Answer(text_of(field(part, "tool_use_id")?, "an answer's call id")?))
		} else {
			Ok(Neither)
		}
	}

	## A call's id, read once the call is known to be a launch.
	call_id : ShaleJson -> Try(Str, Str)
	call_id = |part| text_of(field(part, "id")?, "a call's id")

	## An answer's text: its content when that is a string, else the `text`
	## of each of its parts, a line apart.
	answer_text : ShaleJson -> Try(Str, Str)
	answer_text = |part| {
		content = field(part, "content")?
		match content {
			Text(text) => Ok(text)
			Array(items) => {
				texts = items.map_try(item_text)?
				Ok(Str.join_with(texts.keep_oks(|text| text), "\n"))
			}
			_ =>
				if ShaleJson.is_empty_table(content) {
					Ok("")
				} else {
					Err("`re_find`: its first argument must be a string, not ${type_name(content)}")
				}
		}
	}

	## The answer's `is_error` as Shale's `if` takes it: false when it has
	## none, and true when it is anything but `false`.
	error_flag : ShaleJson -> Try(Bool, Str)
	error_flag = |part|
		match optional_field(part, "is_error")? {
			Ok(Boolean(flag)) => Ok(flag)
			Ok(_) => Ok(True)
			Err(Missing) => Ok(False)
		}
}

## A part of a list content's `text`, `Err(Missing)` for a part without one.
## Shale's `join` writes an integer as its digits.
item_text : ShaleJson -> Try(Try(Str, [Missing]), Str)
item_text = |item|
	match optional_field(item, "text")? {
		Ok(Text(text)) => Ok(Ok(text))
		Ok(Integer(number)) => Ok(Ok(number.to_str()))
		Ok(other) => {
			taken = "`join` takes strings and integers"
			Err("${taken}, and a part's `text` is ${type_name(other)}")
		}
		Err(Missing) => Ok(Err(Missing))
	}

## Whether the value is the string `text`.
is_text : ShaleJson, Str -> Bool
is_text = |value, text|
	match value {
		Text(got) => got == text
		_ => False
	}

## The line's `timestamp`, `""` when it has none (Shale's `get`).
timestamp : ShaleJson -> Try(Str, Str)
timestamp = |line|
	match line {
		Array(items) if !(items.is_empty()) =>
			Err("`get`: a list's keys are integers: this one is a list, given a string")
		Object(_) | Array(_) =>
			match optional_field(line, "timestamp")? {
				Ok(Text(text)) => Ok(text)
				Ok(other) => {
					compared = "`>=` compares a string with a string"
					Err("${compared}, and a line's `timestamp` is ${type_name(other)}")
				}
				Err(Missing) => Ok("")
			}
		_ => Err("`get` takes a map or a list, not ${type_name(line)}")
	}

## A map's member, as Shale's `v.name` reads it: a stop when it is missing.
field : ShaleJson, Str -> Try(ShaleJson, Str)
field = |value, name|
	match optional_field(value, name)? {
		Ok(member) => Ok(member)
		Err(Missing) => Err("no key \"${name}\" in the map (`has_key` asks first)")
	}

## A map's member, or `Missing`; a value that is no map stops.
optional_field : ShaleJson, Str -> Try(Try(ShaleJson, [Missing]), Str)
optional_field = |value, name|
	match value {
		Object(members) =>
			match members.find_first(|(key, _)| key == name) {
				Ok((_, member)) => Ok(Ok(member))
				Err(_) => Ok(Err(Missing))
			}
		Array(items) if items.is_empty() => Ok(Err(Missing))
		_ => Err("`.${name}` reads a map's member, and this is ${type_name(value)}")
	}

## A string where the next step takes one.
text_of : ShaleJson, Str -> Try(Str, Str)
text_of = |value, what|
	match value {
		Text(text) => Ok(text)
		_ => Err("${what} is ${type_name(value)}, not a string")
	}

## Whether Shale's `is_list` holds: JSON written back starts with `[`, so a
## list or the empty table.
is_list : ShaleJson -> Bool
is_list = |value|
	match value {
		Array(_) => True
		_ => ShaleJson.is_empty_table(value)
	}

## Whether `text` sorts at or after `since`, byte by byte, as Shale's `>=`
## compares strings.
at_least : Str, Str -> Bool
at_least = |text, since| {
	text_bytes = text.to_utf8()
	since_bytes = since.to_utf8()
	$at : U64
	var $at = 0
	while $at < text_bytes.len() and text_bytes.get($at) == since_bytes.get($at) {
		$at = $at + 1
	}
	match (text_bytes.get($at), since_bytes.get($at)) {
		(Ok(text_byte), Ok(since_byte)) => text_byte > since_byte
		(_, Err(_)) => True
		(Err(_), Ok(_)) => False
	}
}

## How a value is named in a stop.
type_name : ShaleJson -> Str
type_name = |value|
	match value {
		Object(members) if !(members.is_empty()) => "a map"
		Array(items) if !(items.is_empty()) => "a list"
		Object(_) | Array(_) => "an empty table"
		Text(_) => "a string"
		Integer(_) => "an integer"
		Boolean(_) => "a boolean"
		NoValue => "nil"
	}
