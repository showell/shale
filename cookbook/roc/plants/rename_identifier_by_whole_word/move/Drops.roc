module [ahead]

import Distance
import Geom

## The point ahead, as Geom.RiderPoint says.
ahead : Distance -> Geom.RiderPoint
ahead = |d| { x: d, label: "Geom.RiderPoint" }
