## Under the types, a file with no candidate open (no qualified call, as
## roc-markdown's `package/Bytes.roc`): roc-types never runs, and the run
## ends with nothing left.
NoCalls := [].{
	twice : U64 -> U64
	twice = |n| n * 2
}

expect NoCalls.twice(2) == 4
