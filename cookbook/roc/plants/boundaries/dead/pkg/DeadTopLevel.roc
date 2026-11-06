# A plant for boundaries' `dead`: a package's private module, and in it a member
# nothing calls.
DeadTopLevel := [].{
	private_member = |x| x
}
