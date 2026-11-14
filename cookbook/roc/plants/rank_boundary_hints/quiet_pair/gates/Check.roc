## A gates app, whose uses are tests.
app [main] { game: "../game.roc" }

import game.Lamp

main = Lamp.dim(Lamp.({ watts: 4.0 }))
