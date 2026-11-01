## The root `Typed` is judged on: `roc-types` reads an app's modules.
app [main] { pf: platform "./TypedPf.roc" }

import Typed

main : Str
main = Typed.answer
