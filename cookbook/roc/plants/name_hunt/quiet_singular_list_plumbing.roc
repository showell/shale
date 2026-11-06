# Quiet for every check: bytes are plumbing, and a name that doesn't read as
# one thing, or isn't a list, says nothing false.
boundary : List(U8)
boundary = []

body : Str -> Try(List(U8), [Bad])
body = |text| Err(Bad)

visited : List(Point)
visited = []

draw : Scene -> List(Shape)
draw = |scene| []

trim_hull : List(Point) -> List(Point)
trim_hull = |points| points

with_corner : List(Point), Point -> List(Point)
with_corner = |points, point| points.append(point)

triangle : Shape
triangle = make_shape

scene : U64 -> Set(Point)
scene = |at| make_set

render_onto : List(Shape), U64 -> List(Shape)
render_onto = |out, at| out

area : List(Point), { rows : List(U64) } -> U64
area = |l, { rows }| l.len() + rows.len()
