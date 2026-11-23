# A plant for vocabulary's `retired_name`: a retired word in each place an
# identifier can sit. This comment holds no such word, and neither do strings.
import lib.Journey

Ride := { lean : F64 }.{
	tilt_step : F64
	tilt_step = 0.5

	RideTour : [TourStop, Done]

	stops : { tour_stops : U64 } -> U64
	stops = |plan| plan.tour_stops

	label = |TourStop| "ride"

	last_lean = |tilted_lean| tilted_lean.tilt()
}
