module [near_points]

import game.Geom
import game.Distance

near_points : List Geom.RiderPoint, Geom.RiderPoints -> List Geom.RiderPoint
near_points = |ps, _all| ps.keep_if(|p| Geom.near(p))
