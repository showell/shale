# Quiet for every check: an effect does act, a value makes no claim, and a verb
# that says what happens to its argument is honest.
draw! : Scene => {}

fill_color : U32
fill_color = 0

tree_shapes : Forest -> List(Shape)
tree_shapes = |forest| []

trim_hull : List(Point) -> List(Point)
trim_hull = |points| points

plus : U64, U64 -> U64
plus = |left, right| left + right

redraw_count : U64 -> U64
redraw_count = |number| number

Brush : { fill : U32 }
