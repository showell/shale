## Calls Span's member names on receivers typed as Width, so none counts
## for Span: a parameter, an alias of Width, a record field, and an operator.
import Width

OtherType :: [].{

	grow : Width, Width -> Width
	grow = |w, extra| w.plus(extra)

	shrink : Width.Across, Width -> Width
	shrink = |w, less| w.minus(less)

	frame_minus : Width.Frame -> Width
	frame_minus = |f| f.width.minus(f.width)

	wider : Width, Width -> Bool
	wider = |a, b| a > b
}
