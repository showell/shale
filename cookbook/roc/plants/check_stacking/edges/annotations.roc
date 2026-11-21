module [Pick, pick, run!, handlers, wrapped, effectful, nested, Point]

# A type alias with parameters is read as an annotation.
Pick(a) : a, a, a -> a

# The annotation's lambda is not reported again, and an effectful arrow ends
# the arguments too.
pick : U8, U8, U8 -> U8
pick = |a, b, c| a + b + c

run! : Str, Str, Str => {}
run! = |a, b, c| f!(a, b, c)

# A record type's fields, a function among them with an effectful arrow.
Point : { on_pick : U8, U8, U8 => U8, x : U8, on_drop : (U8, U8) -> U8 }

handlers : { on_a : U8, U8, U8 -> U8, on_b : U8 -> U8 }
handlers = { on_a: |a, b, c| a, on_b: |a| a }

# An annotation wrapped over lines, a bracket open across them.
wrapped : (
	U8,
	U8,
), U8, U8 -> U8
wrapped = |p, b, c| b

effectful : List({ a : U8, b : U8, c : U8 }), Dict(Str, U8), Set(U8) => Bool
effectful = |l, d, s| Bool.True

# An arrow call has no space before its arrow, so it is no function type.
nested = |x| x->f(a, b, c, d, e, g)
