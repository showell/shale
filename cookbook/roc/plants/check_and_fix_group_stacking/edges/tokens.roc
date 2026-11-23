module [dotted, escaped, chars, doubled, called_on]

dotted = f(Angle.Bearing.zero, a.b.c, x.0, y)
escaped = f("a \"quoted\" word", "a \\ slash", 'x', '\'', b)
chars = f('a', 'b', 'c', 'd', 'e', 'g')
doubled = { ..base, a: 1, b: 2, c }
called_on = f(g(x).name, h(y).0, z, w)
