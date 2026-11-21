## count_shale_stops, in Roc on Shale's platform (roc_platform/README.md):
## how an agent's Shale launches went, from a Claude Code transcript. Each
## Bash call's command is paired with the answer it got by the call's id. A
## launch is a heredoc (`shale <<'TAG'`) or a script (`shale path.shale`),
## and each ends one of four ways: it ran, Shale stopped it (`── stopped
## at`), the gate refused the call before Shale saw it, or the harness
## denied it. A stop by the script's own `fail` is a check's verdict,
## counted apart; Shale's other stops are grouped by kind and by message,
## quoted values blanked, so one mistake made twice counts as one line. The
## calls the heredocs make and the programs they `run` are counted too.
##
## Its launcher, cookbook/count_shale_stops.shale, is how it is run, and
## names the newest transcript when no `transcript=` is given:
##
##   shale cookbook:count_shale_stops.shale since=2026-11-14T19:50
##
## The arguments: `transcript` (a Shale path), `since` (an ISO time; only
## the lines from then on count) and `most` (how many lines each list
## shows, 20).
##
## The effects: `Shale.arg!` for the arguments, `Shale.grep_all!` for the
## transcript's lines that can hold a call or an answer (any size of file),
## `Shale.re_match!`, `Shale.re_find!`, `Shale.re_find_all!` and
## `Shale.re_replace!` for the Shale original's own patterns, and
## `Shale.print!` and `Shale.fail!` for the report. ShaleJson, beside this,
## reads a line as Shale's json_read does, its strings by JsonString;
## Transcript reads the fields; Tally counts and prints the tables.
## budget: steps=900000000 reads=50000 read_bytes=3000000000 arena_bytes=3000000000 roc_cpu_ms=60000
app [main!] { pf: platform "../../roc_platform/platform/main.roc" }

import pf.Shale
import ShaleJson
import Tally
import Transcript

## A launch's kind, and how the report names it.
Launch : [Heredoc, Script]

## A part of a message, and the transcript line it is on.
Part : { part : ShaleJson, line_number : U64 }

## What the run was asked: the transcript, from when, and how many lines
## each list shows.
Asked : {
	path : Str,
	since : Str,
	most : I64,
}

## What the run has counted so far.
Counts : {
	calls : Dict(Str, Launch),
	heredocs : U64,
	scripts : U64,
	ends : Dict(Str, U64),
	stop_kinds : Dict(Str, U64),
	stop_lines : Dict(Str, U64),
	builtins : Dict(Str, U64),
	programs : Dict(Str, U64),
}

main! : {} => {}
main! = |{}| {
	path = Shale.arg!("transcript", "")
	if path == "" {
		launcher = "the launcher names the newest under projects:"
		Shale.fail!("count_shale_stops: no transcript= given (${launcher})")
	}
	since = Shale.arg!("since", "")
	most = read_most!({})
	counts = count_transcript!(path, since)
	print_report!(counts, { path, since, most })
}

## The `most=` count: 20 unless given, and any integer.
read_most! : {} => I64
read_most! = |{}| {
	given = Shale.arg!("most", "20")
	match I64.from_str(given) {
		Ok(most) => most
		Err(_) => {
			Shale.fail!("count_shale_stops: most=${given} is not an integer")
			0
		}
	}
}

# ── The count

## Every line that can hold a Bash call or an answer, read in order. A
## line holding neither literal is never read as JSON, as the Shale
## original read none.
count_transcript! : Str, Str => Counts
count_transcript! = |path, since| {
	var $counts = {
		calls: Dict.empty(),
		heredocs: 0,
		scripts: 0,
		ends: Tally.empty,
		stop_kinds: Tally.empty,
		stop_lines: Tally.empty,
		builtins: Tally.empty,
		programs: Tally.empty,
	}
	for hit in Shale.grep_all!(path, "\"tool_") {
		line = hit.text
		is_call = line.contains("\"tool_use\"") and line.contains("\"Bash\"")
		is_answer = line.contains("\"tool_result\"")
		if is_call or is_answer {
			value = get_or_stop!(ShaleJson.read(line), hit.line)
			parts = get_or_stop!(Transcript.recent_parts(value, since), hit.line)
			for part in parts {
				$counts = count_part!($counts, { part, line_number: hit.line })
			}
		}
	}
	$counts
}

## One part of a message counted: a launch and what its heredoc calls, or
## the answer to a launch and how it ended.
count_part! : Counts, Part => Counts
count_part! = |counts, { part, line_number }|
	match get_or_stop!(Transcript.part_kind(part), line_number) {
		ShellCall(command) =>
			match find_launch_kind!(command) {
				Ok(launch) => {
					id = get_or_stop!(Transcript.call_id(part), line_number)
					counted = { ..counts, calls: counts.calls.insert(id, launch) }
					match launch {
						Heredoc => {
							with_heredoc = { ..counted, heredocs: counted.heredocs + 1 }
							count_heredoc!(with_heredoc, command)
						}
						Script => { ..counted, scripts: counted.scripts + 1 }
					}
				}
				Err(NoLaunch) => counts
			}
		Answer(id) =>
			match counts.calls.get(id) {
				Ok(launch) => count_end!(counts, { launch, part, line_number })
				Err(_) => counts
			}
		Neither => counts
	}

## A command's launch: a heredoc, a script, or no launch.
find_launch_kind! : Str => Try(Launch, [NoLaunch])
find_launch_kind! = |command|
	if !(command.contains("shale")) {
		Err(NoLaunch)
	} else if Shale.re_match!(command, "shale[^\\n]*<<'[A-Za-z]+'") {
		Ok(Heredoc)
	} else if Shale.re_match!(command, "shale[^\\n]*\\.shale") {
		Ok(Script)
	} else {
		Err(NoLaunch)
	}

## A heredoc's calls (`name(`) and the programs it runs (`run({"name"`).
count_heredoc! : Counts, Str => Counts
count_heredoc! = |counts, command| {
	called = Shale.re_find_all!(command, "([a-z_][a-z0-9_]*)\\(").map(first_group)
	ran = Shale.re_find_all!(command, "run\\(\\{\"([^\"]+)\"").map(first_group)
	{
		..counts,
		builtins: called.fold(counts.builtins, Tally.bump),
		programs: ran.fold(counts.programs, Tally.bump),
	}
}

## A match of one of Shale's patterns, as `Shale.re_find_all!` gives it.
Match : {
	at : U64,
	text : Str,
	groups : List(Str),
}

first_group : Match -> Str
first_group = |found|
	match found.groups.first() {
		Ok(group) => group
		Err(_) => crash "count_shale_stops: a pattern with a group matched without one"
	}

## How the launch an answer answers ended, by the answer's text.
## An answer to a launch: the launch, the answer's part, and its line.
Answered : {
	launch : Launch,
	part : ShaleJson,
	line_number : U64,
}

count_end! : Counts, Answered => Counts
count_end! = |counts, { launch, part, line_number }| {
	text = get_or_stop!(Transcript.answer_text(part), line_number)
	kind = match launch {
		Heredoc => "heredoc"
		Script => "script"
	}
	match stop_key!(text) {
		Ok(said) if said.starts_with("fail:") => {
			verdict = "stopped by its own `fail` (a check's verdict)"
			{ ..counts, ends: Tally.bump(counts.ends, "${kind}, ${verdict}") }
		}
		Ok(said) => {
			stop_kind =
				match said.split_first(":") {
					Ok({ before, .. }) => before
					Err(_) => said
				}
			{
				..counts,
				ends: Tally.bump(counts.ends, "${kind}, stopped by Shale"),
				stop_kinds: Tally.bump(counts.stop_kinds, stop_kind),
				stop_lines: Tally.bump(counts.stop_lines, said),
			}
		}
		Err(NoStop) => {
			ended =
				if text.contains("shale-gate:") {
					"refused by the gate"
				} else if get_or_stop!(Transcript.error_flag(part), line_number) {
					if text.contains("denied") {
						"denied by the harness"
					} else {
						"nonzero exit, no stop line"
					}
				} else {
					"ran"
				}
			{ ..counts, ends: Tally.bump(counts.ends, "${kind}, ${ended}") }
		}
	}
}

## A stop's line, with quoted values blanked, for grouping: what it says
## after its place, cut before its first ` (`, at most 110 bytes and `…`.
stop_key! : Str => Try(Str, [NoStop])
stop_key! = |text|
	match Shale.re_find!(text, "── stopped at [^:]+:[0-9]+:[0-9]+[^:]*: ([^\\n]*)") {
		Found(found) => {
			said = first_group(found)
			uncut =
				match said.split_first(" (") {
					Ok({ before, .. }) => before
					Err(_) => said
				}
			blanked = Shale.re_replace!(uncut, "\"[^\"]*\"", "\"…\"")
			Ok(cut_to_bytes(blanked, 110))
		}
		NoMatch => Err(NoStop)
	}

## The text cut to `most` bytes and `…` when it is longer. A cut inside a
## character goes back to the character's start: the Shale original cut
## the bytes, which a Roc `Str` cannot hold.
cut_to_bytes : Str, U64 -> Str
cut_to_bytes = |text, most| {
	bytes = text.to_utf8()
	if bytes.len() <= most {
		text
	} else {
		cut = List.from_iter(U64.until(0, 4)).find_first(
			|back| Str.from_utf8(bytes.take_first(most - back)).is_ok(),
		)
		kept =
			match cut {
				Ok(back) => Str.from_utf8_lossy(bytes.take_first(most - back))
				Err(_) => crash "count_shale_stops: no character starts within 4 bytes"
			}
		"${kept}…"
	}
}

## The value, or the run stopped naming the transcript line that gave the
## problem.
get_or_stop! : Try(a, Str), U64 => a
get_or_stop! = |got, line_number|
	match got {
		Ok(value) => value
		Err(why) => {
			Shale.fail!("count_shale_stops: line ${line_number.to_str()}: ${why}")
			crash "count_shale_stops: Shale.fail! returned"
		}
	}

# ── The report

## The transcript, the launches, how they ended, Shale's stops by kind and
## by message, and the heredocs' calls and programs, as the Shale original
## printed them.
print_report! : Counts, Asked => {}
print_report! = |counts, { path, since, most }| {
	since_note = if since == "" {
		""
	} else {
		", since ${since}"
	}
	heredocs = "${counts.heredocs.to_str()} heredocs"
	launches = "${heredocs}, ${counts.scripts.to_str()} script launches"
	Shale.print!(path)
	Shale.print!("${launches}${since_note}")
	sections = [
		("", counts.ends),
		("Shale's stops, by kind (a script's own `fail` left out):", counts.stop_kinds),
		("Shale's stops, by message:", counts.stop_lines),
		("calls in heredocs (builtins and the script's own functions):", counts.builtins),
		("programs heredocs run:", counts.programs),
	]
	for (heading, tally) in sections {
		Shale.print!("")
		if heading != "" {
			Shale.print!(heading)
		}
		Shale.print!(Tally.ranked_table(tally, most))
	}
}
