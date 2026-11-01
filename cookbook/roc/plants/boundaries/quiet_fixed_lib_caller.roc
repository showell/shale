# Quiet for every check: the calls of lib/Scale.roc's `times` and
# pkg/Geo.roc's `half`, each with the same first argument.
double : I64 -> I64
double = |x| Scale.times(2, x)

doubled : I64 -> I64
doubled = |y| Scale.times(2, y)

halved : I64 -> I64
halved = |x| Geo.half(2, x)

halve : I64 -> I64
halve = |y| Geo.half(2, y)

thirds : I64 -> I64
thirds = |x| Hidden.part(3, x)

third : I64 -> I64
third = |y| Hidden.part(3, y)
