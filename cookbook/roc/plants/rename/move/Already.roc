module [start]

import RiderView
import Geom

start : RiderView.RaisedPoint -> Geom.RiderPoint
start = |p| p.ground
