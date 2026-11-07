## A module whose top-level values are named by punned record fields, which
## roc-ast keeps with no identifier node.
import Shade
import Probe

Pun :: { shades : List(U64), count : U64 }.{

	## The pun every app draws.
	drawn : Pun
	drawn = framed
}

## Named by `drawn`, in the block, so production's.
framed : Pun
framed = { shades, count: 1 }

## Named by an expect and by `framed`'s punned `{ shades }`, so production's,
## and Shade with it.
shades : List(U64)
shades = [Shade.dark]

expect shades.len() == 1

## Named only by the expect's punned `{ probed, .. }`, so a test's, and Probe
## with it. A lone `{ probed }` is a block holding `probed`, no record.
probed : U64
probed = Probe.value

expect { probed, count: 1 } == { probed: 1, count: 1 }
