## The root the Packaged plant is judged on: an app naming the package `pk`.
app [main] { pf: platform "./TypedPf.roc", pk: "./PlantPkg.roc" }

import Packaged
import pk.Meter

main : Str
main = Packaged.twice(Meter.two).count().to_str()
