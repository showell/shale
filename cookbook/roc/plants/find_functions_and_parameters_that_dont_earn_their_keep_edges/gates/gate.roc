# Edges for find_functions_and_parameters_that_dont_earn_their_keep: a gate, named by `tests=`, whose calls count
# as a test's.
app [main] { pf: platform "../typed/EdgePf.roc" }

main = Outer.shallow(lonely(1))
