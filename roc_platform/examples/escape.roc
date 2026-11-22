## The policy holds: a Roc script that reaches past Shale's guards stops
## with Shale's own refusal. `try=` picks the reach:
##
##   shale roc_platform/run.shale script=roc_platform/examples/escape.roc try=climb     a path out of the root (`..`)
##   shale roc_platform/run.shale script=roc_platform/examples/escape.roc try=absolute  an absolute path
##   shale roc_platform/run.shale script=roc_platform/examples/escape.roc try=bash      a program `run` refuses
##   shale roc_platform/run.shale script=roc_platform/examples/escape.roc try=write     a write out of the root
##   shale roc_platform/run.shale script=roc_platform/examples/escape.roc try=crash     a Roc `crash`
##   shale roc_platform/run.shale script=roc_platform/examples/escape.roc try=fail      the script's own stop, `Shale.fail!`
##   shale roc_platform/run.shale script=roc_platform/examples/escape.roc try=arena     past the arena's budget
##   shale roc_platform/run.shale script=roc_platform/examples/escape.roc try=missing   a missing file, a value
##   shale roc_platform/run.shale script=roc_platform/examples/escape.roc try=ok        a run Shale lets through
##   shale roc_platform/run.shale script=roc_platform/examples/escape.roc try=loop      a pure loop that never ends
##   shale roc_platform/run.shale script=roc_platform/examples/escape.roc try=spin      a pure loop inside its budget
##   shale roc_platform/run.shale script=roc_platform/examples/escape.roc try=module    a path out of the root, from a module
##   shale roc_platform/run.shale script=roc_platform/examples/escape.roc try=package   a package's function, a value
##
## It is a script of three files and a package: this root, Reach.roc beside
## it (its own module, which reaches the system through the platform as this
## file does) and kit/ (a package by a relative path, which reaches nothing).
##
## The Roc's CPU budget is small here, so `loop` stops soon.
## budget: roc_cpu_ms=400
app [main!] {
	kit: "kit/main.roc",
	pf: platform "../platform/main.roc",
}

import pf.Shale
import Reach
import kit.Plural

main! : {} => {}
main! = |{}| {
	match Shale.arg!("try", "climb") {
		"climb" => {
			_ = Shale.read!("../outside.txt")
			Shale.print!("read past the root: the policy did NOT hold")
		}
		"absolute" => {
			_ = Shale.exists!("/etc/passwd")
			Shale.print!("asked of an absolute path: the policy did NOT hold")
		}
		"bash" => {
			r = Shale.run!({ argv: ["bash", "-c", "echo hi"], keep: Tail(5), timeout_ms: 5000, want: "" })
			Shale.print!("bash ran (${r.out}): the policy did NOT hold")
		}
		"write" => {
			Shale.write!("../escaped.txt", "out")
			Shale.print!("wrote past the root: the policy did NOT hold")
		}
		"crash" => {
			crash "a promise the script broke"
		}
		"fail" => Shale.fail!("the script's own stop")
		"arena" => {
			big = Str.repeat("x", 100_000_000)
			Shale.print!("made ${U64.to_str(Str.count_utf8_bytes(big))} bytes past the arena: the budget did NOT hold")
		}
		"loop" => {
			var $n = 0
			while $n >= 0 {
				$n = $n + 1
			}
			Shale.print!("the loop ended: the budget did NOT hold")
		}
		"spin" => {
			var $n = 0
			while $n < 200_000 {
				$n = $n + 1
			}
			Shale.print!("spun ${U64.to_str($n)} times, inside the budget")
		}
		"module" => Reach.climb!({})
		"package" => Shale.print!("a package's value: ${Plural.of(3, "file")}")
		"missing" =>
			match Shale.read!("no/such/file.txt") {
				Found(_) => Shale.print!("found a file that is not there")
				Missing => Shale.print!("Missing, as a value: the script goes on")
			}
		_ => {
			r = Shale.run!({ argv: ["git", "--version"], keep: Head(1), timeout_ms: 5000, want: "" })
			Shale.print!("git ran, exit ${I64.to_str(r.code)}: ${Str.trim(r.out)}")
		}
	}
}
