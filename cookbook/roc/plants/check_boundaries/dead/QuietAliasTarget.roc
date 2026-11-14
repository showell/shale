# Quiet for boundaries' `dead`, read with quiet_dead_reached.roc, which reaches
# this member through an import's alias.
QuietAliasTarget := [].{
	## Its one caller is a plant.
	through_alias = |x| x
}
