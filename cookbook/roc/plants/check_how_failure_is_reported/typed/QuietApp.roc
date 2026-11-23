## The root the typed plants are judged on: `roc-types` reads an app's
## modules.
app [main] { pf: platform "./QuietPf.roc" }

import NumberFallbackIndex
import QuietNumberFallbackCount

main : Str
main = "${NumberFallbackIndex.seat_count([1], 0).to_str()}${QuietNumberFallbackCount.top([2]).to_str()}"
