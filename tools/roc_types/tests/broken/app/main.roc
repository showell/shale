## The nested fixture with a type error in `inner`.
app [main] { pf: platform "../../nested/pf/main.roc", outer: "../outer/main.roc" }

import outer.Outer

main : Str
main = Outer.greet("roc")
