# Edges for errors' `set_fallback`: deep fields, a method on a field, and
# the stores that are not judged.
deep = |r, s| List.set(r.a.b, s, 1) ?? r.a.b

other_field = |r, s| List.set(r.a.b, s, 1) ?? r.a.c

on_field = |game, s| game.cells.set(s, 1) ?? game.cells

two_args = |l, s| List.set(l, s) ?? l

called = |l, s| List.set(l, s, 1) ?? f(l)

same_call = |l, s| List.set(f(l), s, 1) ?? f(l)

nested = |l| List.set(List.set(l, 0, 1) ?? l, 1, 2) ?? l
