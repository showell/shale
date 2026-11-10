module [resting_lens_shapes]

# The widest group of two on this long line is a type application among an
# annotation's arguments. Once it stacks, `roc fmt` puts the arguments after it
# on a line of their own, and the group must stay stacked: joined, they would
# make this line again.
resting_lens_shapes : Try(List(Str), [BehindTheCameraAndWellOutOfSightOfTheRider]), List(U8) -> List(Str) # findings 1 advice stack
resting_lens_shapes = |_sighting, _darkness| []
