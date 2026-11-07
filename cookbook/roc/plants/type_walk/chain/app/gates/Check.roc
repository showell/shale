## A gates app, which reads Ride through the package its header names, so its
## uses of Ride's members, and its annotations, are tests'.
app [main!] { game: "../main.roc" }

import game.Ride

main! = |_args| Ok({})

## How many legs a ride has.
check : Ride -> U64
check = |ride| Ride.legs_of(ride).len()
