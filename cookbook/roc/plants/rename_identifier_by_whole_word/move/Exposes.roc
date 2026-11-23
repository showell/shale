module [far]

import Geom exposing [near]

far : Geom.RiderPoint -> Bool
far = |p| !near(p)
