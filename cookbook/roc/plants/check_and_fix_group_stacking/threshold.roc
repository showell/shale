module [
	Three,
	Two,
	chain,
	counted,
	five,
	nested_pair,
	pair,
	path,
	rest,
	shorthand,
	six,
	value_three,
]

# Five tokens sit on one line, and six stack.
five = f(a, b, c, d, e) # findings 0
six = f(a, b, c, d, e, g) # findings 1

# A name and a type make two tokens, so three typed fields stack.
Two : { a : Foo, b : Bar } # findings 0
Three : { a : Foo, b : Bar, c : Baz } # findings 1

# A dotted path is one token, however long, and so is a string or a character.
path = f(A.b, a.b.c, "two words", 'c', d) # findings 0
chain = f(g(x).y.z, a, b, c) # findings 0
counted = f(A.b, a.b.c, "two words", 'c', d, e) # findings 1

value_three = { a: 1, b: 2, c: 3 } # findings 1
shorthand = { a, b, c, d, e } # findings 0

# Two items stay side by side, whatever they hold.
pair = f(g(a, b), h(c, d)) # findings 0
nested_pair = [{ x: 1, y: 2 }, { x: 3, y: 4 }] # findings 0

# A rest pattern is no token.
rest = |{ a, b, c, d, e, .. }| a # findings 0
