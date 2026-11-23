## A rewrite that `roc check` passes and `roc test` refuses: `s.shout()`
## would find Str's own `with_ascii_uppercased`, which uppercases, where
## Loud's adds a `!`. Both are `Str -> Str`, so the check passes, and the
## expect catches it. The summary of `roc test` changes, so the whole file is
## put back, and its other call, which would turn alone, stays too.
import Loud

TestChanged := [].{
	shout : Str -> Str
	shout = |s| Loud.with_ascii_uppercased(s)

	size : List(U8) -> U64
	size = |bytes| List.len(bytes)
}

expect TestChanged.shout("a") == "a!"
expect TestChanged.size([1]) == 1
