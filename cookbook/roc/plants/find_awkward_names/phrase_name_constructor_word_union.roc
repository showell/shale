# A plant for `phrase_name`: only `from_*` may hold its type inside a union. A
# constructor's word needs the type itself.
Clock := U32.{
	at_step : U32 -> [Ticked(Clock), Stopped]
	at_step = |number| Stopped
}
