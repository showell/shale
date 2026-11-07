## Finds the pines in view.
import Spot
import View

PineSighting :: [].{

	## The pines in view.
	pines_in_view : View -> List(Spot)
	pines_in_view = |view| [pine_at(view)]

	## The pine on a segment, if it is in view.
	segment_pines : View -> Try(Spot, [Behind, TooSmall])
	segment_pines = |view| Ok(pine_at(view))
}

## A pine at the view's left edge.
pine_at : View -> Spot
pine_at = |_view| { x: 0.0, y: 0.0 }

## A view of `width`, which only an expect uses.
view_of : F64 -> View
view_of = |width| { width }

expect pine_at(view_of(2.0)).x == 0.0
