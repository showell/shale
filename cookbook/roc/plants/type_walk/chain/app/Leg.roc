## One leg of a ride.
import shared.Unit

Leg :: {
	length : Unit.Metres,
	label : Tagged(Str),
}.{

	## Where a leg starts and ends.
	Stretch : { from : Leg, to : Leg }

	## A leg's length.
	length_of : Leg -> Unit.Metres
	length_of = |leg| leg.length
}

## A value with a label.
Tagged(a) : { value : a, label : Str }
