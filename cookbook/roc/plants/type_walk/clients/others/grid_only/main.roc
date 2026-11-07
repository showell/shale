## A client app that imports the package's Grid but not its Dot.
app [main] { kit: "../../pkg/main.roc" }

import kit.Grid

main = Grid.start
