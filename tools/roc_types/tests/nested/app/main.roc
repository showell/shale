## The app names `outer` and not `inner`: roc-types must resolve `outer`'s own
## package, as `roc check` does.
app [main] { pf: platform "../pf/main.roc", outer: "../outer/main.roc" }

import outer.Outer

main : Str
main = Outer.greet("roc")
