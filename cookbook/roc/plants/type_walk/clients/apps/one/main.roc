## A client app that names the package `kit`.
app [main] { kit: "../../pkg/main.roc" }

import kit.Dot

## Moves a dot right and out.
moved : Dot -> Dot
moved = |d| Dot.scale(Dot.shift(d))

main = moved
