## The root the typed edges are judged on: roc-types reads an app's modules,
## and Unreached.roc is left out of them on purpose.
app [main] { pf: platform "../../typed/QuietPf.roc" }

import Inferred

main : Str
main = if Inferred.is_wide(3) or Inferred.has_any([1]) == 1 or Inferred.with_more([1]).len() == 2 or Inferred.marked(1) "y" else "n"
