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

## A count that only the next value reads, which only an expect reads, so
## its use of `Ride.count` is a test's.
sample_count : U32
sample_count = Ride.count

doubled_count : U32
doubled_count = sample_count + sample_count

## A plan that only an expect reads, so its annotation is named as a test's.
sample_plan : Ride.Plan
sample_plan = Short

expect doubled_count == 6
expect sample_plan == Short
expect Ride.noted({ legs: [], name: "x" }, { marks: [] }) == "x"
