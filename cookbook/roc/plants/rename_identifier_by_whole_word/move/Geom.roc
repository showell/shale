module [near]

## The old module: `RiderPoint` has moved out of it by hand.
near : { x : F32, y : F32 } -> Bool
near = |p| p.x < 10
