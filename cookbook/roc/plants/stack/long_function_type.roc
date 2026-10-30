module [granter_form!]

# The widest group of two on this long line is a function type's
# arguments, which take no trailing comma.
granter_form! : (Str, Str => Try(Str, [Denied(Str), NotFound(Str)])) => Try(Str, [Denied(Str), NotFound(Str)])
granter_form! = |grant!| grant!("who", "what")
