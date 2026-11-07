## A ride is a list of legs, with a name.
import Leg
import Note
import Hidden

Ride :: { legs : List(Leg), name : Str }.{

	## How long a ride's plan is.
	Plan : [Short, Long(Leg.Stretch)]

	count : U32
	count = 3

	## The legs of a ride, in order.
	legs_of : Ride -> List(Leg)
	legs_of = |ride| {
		marker = "BODY_MARKER"
		ride.legs
	}

	## A ride's name with a note after it.
	noted : Ride, Note -> Str
	noted = |ride, _note| ride.name
}

## A helper outside the module's block, so not part of the type.
helper : Ride -> Hidden
helper = |_ride| Hidden.none
