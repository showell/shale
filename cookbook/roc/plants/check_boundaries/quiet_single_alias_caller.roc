# Quiet for boundaries' `single`: it calls single_alias_target.roc's member
# through an import's alias.
import SingleAliased as Short

go = |x| {
	y = Short.once(x)
	y + 1
}

expect go(1) == 2
