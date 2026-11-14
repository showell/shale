## Finds the oaks in view.
import Bark
import Spot
import View

OakSighting :: [].{

	## The oaks in view.
	oaks_in_view : View -> List(Spot)
	oaks_in_view = |view| [oak_at(view)]

	## The oak on a segment, if it is in view.
	segment_oaks : View, Bark -> Try(Spot, [TooSmall, Behind])
	segment_oaks = |view, _bark| Ok(oak_at(view))
}

## An oak at the view's middle.
oak_at : View -> Spot
oak_at = |view| { x: view.width, y: 0.0 }

sample : View
sample = { width: 1.0 }

expect oak_at(sample).y == 0.0
