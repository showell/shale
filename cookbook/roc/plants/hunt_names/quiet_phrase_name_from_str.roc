# Quiet for every check: a module with no type of its own whose `from_str` has
# no mirror.
SerialNumber :: [].{
	from_str : Str -> Try(U32, [NotASerialNumber])
	from_str = |text| Err(NotASerialNumber)
}
