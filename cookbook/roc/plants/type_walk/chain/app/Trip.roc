## A trip: a ride and its legs. Its definition, a member and a helper name
## Ride's types; its own `Plan` is another type of the same bare name.
import Ride
import Leg

Trip :: { ride : Ride, legs : List(Leg) }.{

	## The plan of a trip's ride.
	plan_of : Trip -> Ride.Plan
	plan_of = |_trip| Short

	## How a trip is made, which is no ride's plan.
	Plan : [Walk, Cycle]

	own_plan : Trip -> Plan
	own_plan = |_trip| Walk
}

## A private helper that takes a ride.
start : Ride -> Trip
start = |ride| { ride, legs: [] }
