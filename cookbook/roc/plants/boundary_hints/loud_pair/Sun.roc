## The sun, whose looks Backdrop reads to draw it: the plant's fed-to-one
## members.
Sun :: { radius : F64, warmth : F64 }.{

	## Only Backdrop's sun_shapes uses it, and builds a Disc beside it.
	look : Sun -> F64
	look = |Sun.({ radius, warmth: _ })| radius

	## Only Backdrop's sun_shapes calls it, as a method on a Sun.
	glow : Sun -> F64
	glow = |Sun.({ radius: _, warmth })| warmth

	## Only Backdrop's tinted uses it, which builds no shape.
	tint : Sun -> F64
	tint = |Sun.({ radius: _, warmth })| warmth * 2.0

	## Backdrop uses it in an expect alone.
	test_only : Sun -> F64
	test_only = |Sun.({ radius, warmth: _ })| radius * 2.0

	## Backdrop and Mountains both use it.
	shared : Sun -> F64
	shared = |Sun.({ radius, warmth: _ })| radius * 3.0
}
