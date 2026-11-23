# A plant for errors' `optional_tag`: an optional built where no type says
# what it is, its error left to inference.
first = |l|
	match l {
		[x, ..] => Ok(x)
		[] => Err(Nothing)
	}
