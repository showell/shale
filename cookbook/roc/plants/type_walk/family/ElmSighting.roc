## Finds the elms in view, and breaks the family's pattern twice: its list of
## sightings is `visible_elms`, not `elms_in_view`, and its segment's
## sighting answers a smaller error set.
import Spot
import View

ElmSighting :: [].{

	## The elms in view.
	visible_elms : View -> List(Spot)
	visible_elms = |view| [{ x: view.width, y: 1.0 }]

	## The elm on a segment, if it is in front of the camera.
	segment_elms : View -> Try(Spot, [Behind])
	segment_elms = |view| Ok({ x: view.width, y: 1.0 })
}
