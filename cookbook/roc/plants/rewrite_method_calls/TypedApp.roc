## The root the typed plants are judged on: `roc-types` reads an app's
## modules.
app [main] { pf: platform "./TypedPf.roc" }

import Typed
import Proven
import ProvenFails

main : Str
main = "${Typed.answer}${Proven.shout("a")}${ProvenFails.size(ProvenFails.bytes).to_str()}"
