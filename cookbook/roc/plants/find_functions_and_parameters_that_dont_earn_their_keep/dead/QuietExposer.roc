# Quiet for boundaries' `dead`, read with quiet_dead_reached.roc, which names
# `by_exposing` in its import and reaches `by_type_alias` through a type alias.
QuietExposer := [].{
	by_exposing = |x| x
	by_type_alias = |x| x
}
