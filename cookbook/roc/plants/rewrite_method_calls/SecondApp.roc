## The second root of the plant `Second`, which `TypedApp` does not reach:
## `root=TypedApp.roc,SecondApp.roc` judges each file on the first root
## that reaches it.
app [main] { pf: platform "./TypedPf.roc" }

import Second

main : Str
main = Second.size([1]).to_str()
