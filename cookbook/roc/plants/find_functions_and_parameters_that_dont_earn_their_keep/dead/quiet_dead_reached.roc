# Quiet for boundaries' `dead`: the ways a function is reached without an
# identifier naming it where it is defined.
app [reach] { pf: platform "../plat/main.roc" }

import QuietAliasTarget as Target
import QuietExposer exposing [by_exposing]

Aliased : QuietExposer

reach = |x| {
	helper_in_shorthand = 1
	bundle = { from_shorthand, helper_in_shorthand }
	(Target.through_alias(x), bundle, by_exposing(x), Aliased.by_type_alias(x))
}

from_shorthand = |x| x
