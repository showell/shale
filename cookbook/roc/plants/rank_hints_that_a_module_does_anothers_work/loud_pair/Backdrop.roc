## The backdrop, which draws the sun for it and names the sky's stops.
import Sky
import Sun

Backdrop :: [].{

	## Borrows Sky's noun.
	sky_top_stop : Str
	sky_top_stop = Sky.top_color

	## A skyline is no sky, so this borrows nothing.
	skyline_height : F64
	skyline_height = 3.0

	## Draws the sun from its look and its glow.
	sun_shapes : Sun -> List(Shape)
	sun_shapes = |sun| [Disc({ x: 0.0, y: 0.0, radius: Sun.look(sun) + sun.glow() })]

	## A number from the sun's tint, no shape.
	tinted : Sun -> F64
	tinted = |sun| Sun.tint(sun) + Sun.shared(sun)

	## A ring round the sun, drawn by a helper; its annotation gives a Shape.
	halo : Sun -> Shapes.Shape
	halo = |sun| ring(Sun.ring_size(sun))
}

ring : F64 -> Shapes.Shape
ring = |size| ring_of(size)

expect Sun.test_only(Sun.({ radius: 1.0, warmth: 1.0 })) == 2.0
