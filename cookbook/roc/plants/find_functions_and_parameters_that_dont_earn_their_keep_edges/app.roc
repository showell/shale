# Edges for find_functions_and_parameters_that_dont_earn_their_keep: an app's provided name has its caller outside
# the tree, so it is never `dead`.
app [main!] { pf: platform "./typed/EdgePf.roc" }

main! = |_args| helper(1)

helper = |x| x
