module [RiderPoint, RaisedPoint, raise]

import Geom

RiderPoint : { x : F32, y : F32 }

RaisedPoint : { ground : Geom.RiderPoint, height : F32 }

raise : RaisedPoint -> F32
raise = |p| p.height
