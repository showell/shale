## The root the typed edges are judged on: roc-types reads an app's modules,
## and Unreached.roc is left out of them on purpose.
app [main] { pf: platform "../../typed/QuietPf.roc" }

import TypedFallbacks

main : Str
main = TypedFallbacks.summary([1], ["a"])
