# Edges for check_boundaries: a gate, named by `tests=`, whose calls count
# as a test's.
app [main] { pf: platform "../typed/EdgePf.roc" }

main = Outer.shallow(lonely(1))
