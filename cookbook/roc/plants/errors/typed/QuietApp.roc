## The root the typed plants are judged on: `roc-types` reads an app's
## modules.
app [main] { pf: platform "./QuietPf.roc" }

import PlaceholderIndex
import QuietPlaceholderCount

main : Str
main = "${PlaceholderIndex.seat_count([1], 0).to_str()}${QuietPlaceholderCount.top([2]).to_str()}"
