## An app beside the package that is no widget.
app [main] { lib: "../lib/main.roc" }

import lib.Spot

main = |s| Spot.step(s)
