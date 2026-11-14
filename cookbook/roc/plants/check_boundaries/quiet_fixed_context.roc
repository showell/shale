# Quiet for boundaries' `fixed`: context passed down, the same variable or
# field at every call, is the caller's value handed on, not a constant.
greet : Str, Str -> Str
greet = |me, s| Str.concat(me, s)

hi : Str -> Str
hi = |me| greet(me, "hi")

bye : Str -> Str
bye = |me| greet(me, "bye")

hello : Str -> Str
hello = |who| Str.concat("hello ", who)

welcome : { me : Str } -> Str
welcome = |req| hello(req.me)

welcome_back : { me : Str } -> Str
welcome_back = |req| hello(req.me)
