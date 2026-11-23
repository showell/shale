# Edges for find_functions_and_parameters_that_dont_earn_their_keep' argument texts: an arrow call's receiver, a
# record's fields (one a shorthand), a character, a negative number, a
# fraction, a module's constant, a tag, `Bool.True` (no literal), a slot one
# call leaves out, a variable handed on, and three calls.
padded : Str, U64 -> Str
padded = |s, n| Str.concat(s, n.to_str())

left = |_x| "x"->padded(3)

right = |_y| "x"->padded(3)

find_in : Str, { needle : Str, at : U64 } -> U64
find_in = |_hay, { needle, at }| at

first_a = |hay, needle| find_in(hay, { needle, at: 0 })

first_b = |hay, needle| find_in(hay, { needle, at: 0 })

marked = |_c, _k, s| s

one = |s| marked('c', -1, s)

two = |s| marked('c', -1, s)

three = |s| marked('c', -1, s)

scaled = |n, by, cap| n * by + cap

scale_a = |n| scaled(n, 1.5, Limits.max)

scale_b = |n| scaled(n, 1.5, Limits.max)

begun = |_state, s| s

beg_a = |s| begun(Begin, s)

beg_b = |s| begun(Begin, s)

flagged = |_flag, s| s

flag_a = |s| flagged(Bool.True, s)

flag_b = |s| flagged(Bool.True, s)

optional = |a, _b| a

opt_a = |x| optional(x, 1)

opt_b = |x| optional(x)

handed = |me, s| Str.concat(me, s)

hand_a = |me| handed(me, "a")

hand_b = |me| handed(me, "b")

escaped = |s, sep| Str.concat(s, sep)

esc_a = |s| escaped(s, "\"")

esc_b = |s| escaped(s, "\"")

passed = |xs| List.map(xs, padded)
