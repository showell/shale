# A plant for `phrase_name`: `with_*` that gives neither its own module's type
# nor its first argument's is no changed copy.
with_pig_off : F64 -> RiderState
with_pig_off = |degrees| RiderState.turned(degrees)
