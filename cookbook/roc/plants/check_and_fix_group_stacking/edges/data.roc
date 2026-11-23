module [points, shape, corner, packed, spaced, mixed, pair_list]

# Numeric data stays packed at any width: a number list, a negative written
# `(0.0 - 0.5)`, and a flat number record among a number list's items.
points = [0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 5.5, 6.0, 6.5, 7.0, 7.5, 8.0, 8.5, 9.0, 9.5]
shape = [{ x: 0.5, y: (0.0 - 0.25) }, { x: 1.5, y: 2.5 }, { x: (0.0 - 1.5), y: 3.25 }, { x: 4.0, y: 5.0 }, { x: 6, y: 7 }]
corner = { x: 0.5, y: (0.0 - 0.25), z: 1.0, w: 2.0 }
packed = f([1, 2, 3, 4, 5, 6], g(a, b, c, d, e, h), "a string to take the line past the width of a line, and on")
spaced = [ 1 , 2 , 3 , ]
mixed = [(0.0 - 0.5), 1, 2, three, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24]
pair_list = [{ x: 1, y: 2, }, { x: 3, y: 4 }, { x: 5, y: 6 }, { x: 7, y: 8 }, { x: 9, y: 10 }, { x: 11, y: 12 }]
