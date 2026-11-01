## roc-types' smoke app (.github/workflows/roc-types.yml): its platform and
## package specs have no `./`, which the `roc` CLI takes and the compiler's
## own discovery refuses.
app [main] {
	pf: platform "platform/main.roc",
	lib: "lib/main.roc",
}

import lib.Greet

main : Str
main = Greet.hello("smoke")
