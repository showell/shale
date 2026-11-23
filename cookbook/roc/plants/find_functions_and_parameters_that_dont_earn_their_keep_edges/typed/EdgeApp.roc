## The root the typed edges are judged on: roc-types reads an app's modules,
## and Unreached.roc is left out of them on purpose.
app [main] { pf: platform "./EdgePf.roc" }

import Flags

main : Str
main = {
	a = Flags.shown("a", Bool.True)
	b = Flags.shown("b", Bool.False)
	c = Flags.shown("c", Bool.True)
	d = Flags.boxed({ text: "d", loud: Bool.True })
	e = Flags.after(Bool.True, "e")
	"${a}${b}${c}${d}${e}"
}
