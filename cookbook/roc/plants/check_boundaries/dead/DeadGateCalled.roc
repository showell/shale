# A plant for boundaries' `dead` under `test_reach=dead`: only the gate
# gates/gate_app.roc, read as a test, calls this member.
DeadGateCalled := [].{
	only_gate_calls = |x| x
}
