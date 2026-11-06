## A copy of `Shared.Size`, as a package keeps when it can't import its platform.
Mirror := [].{
	Size : { wide : Length, high : Length }

	Length : U64

	Mode : [Fast, Slow]
}
