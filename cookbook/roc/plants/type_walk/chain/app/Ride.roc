## A ride is a list of legs, with a name.
import Leg
import Note
import Hidden
import Speed
import Tally
import Sample

Ride :: { legs : List(Leg), name : Str }.{

	## How long a ride's plan is.
	Plan : [Short, Long(Leg.Stretch)]

	## How many rides a day allows.
	count : U32
	count = 3

	cruising : Speed
	cruising = CONSTANT_MARKER

	is_eq : _
	is_eq = |a, b| a == b

	## The legs of a ride, in order.
	legs_of : Ride -> List(Leg)
	legs_of = |ride| {
		marker = Tally.mark("BODY_MARKER")
		ride.legs
	}

	## A ride's name with a note after it.
	noted : Ride, Note -> Str
	noted = |ride, _note| ride.name
}

## A helper outside the module's block, so not part of the type.
helper : Ride -> Hidden
helper = |_ride| Hidden.none

## A value only the expect below reads, so its annotation's and its body's
## uses of Sample are tests'.
sample_one : Sample
sample_one = Sample.one

expect sample_one == Sample.one

## Tally is also used in production, in `legs_of`, so it is no test's alone.
expect Tally.mark("x") == "x"
