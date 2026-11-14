# A plant for `phrase_name`: a module's own type doesn't make a constructor of
# a word no constructor starts with, as with `Scene.in_view`.
Scene := {}.{
	in_view : Str -> Scene
	in_view = |view| Scene.make(view)

	within : Scene, Scene -> Scene
	within = |scene, limit| Scene.clamp(scene, limit)
}
