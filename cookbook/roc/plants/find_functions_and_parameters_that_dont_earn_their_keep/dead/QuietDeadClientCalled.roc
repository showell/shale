# Quiet for boundaries' `dead`: the client clients/client_app.roc calls this
# member, and nothing in the files checked does.
QuietDeadClientCalled := [].{
	## Its one caller is a client.
	by_client = |x| x
}
