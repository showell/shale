module [go, state, other]

import Ride
import Other

go : U64 -> Ride.Rider
go = |n| Ride.start(n)

state : Ride.RiderState
state = Resting

other : Other.Rider
other = Other.make(1)

Rider : Str
