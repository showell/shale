module [first, pick]

import game.Other exposing [tilt_now]

# A retired word in a `where` method, an import's exposed name and an `as`
# pattern's name: three findings, one a line.
pick : a -> U8 where [a.tilt_of : a -> U8]
pick = |x| x.measure()

first : (U8, U8) -> U8
first = |pair| match pair {
	(a, _) as tilt_pair => a
}
