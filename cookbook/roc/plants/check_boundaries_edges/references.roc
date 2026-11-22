# Edges for check_boundaries' references: through `import ... as`, from an
# `expect`, from outside any declaration, and as a value.
import Shapes as S

sized = |w| S.area(w, 2)

sized_again = |w| S.area(w, 2)

pair_of = |x| (x, x)

tested = |x| pair_of(x)

expect tested(1) == (1, 1)

expect pair_of(2) == (2, 2)

sides = |xs| List.map(xs, S.side)

expect S.unit({}) == 1
