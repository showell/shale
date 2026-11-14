# A plant for boundaries' `dead` under `test_reach=dead`: only an `expect` in
# the client clients/client_app.roc reaches this member.
DeadClientExpect := [].{
	only_in_client_expect = |x| x
}
