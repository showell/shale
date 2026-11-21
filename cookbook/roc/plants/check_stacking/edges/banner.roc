module [tabbed, unicode, row, called]

# ──────────────────────────────────────── a banner, forty box-drawing characters
# A box-drawing character is three bytes, and the width counts bytes, so the banner above is long.

tabbed = |a|
		f(a, "a string of some length, long enough to pass the width with tabs", b, c)
unicode = f("é", "ü", "—", 'x', a.b, c)
row = ("a", "b", "c", 1, 2, Red)
called = f("a", "b", "c", 1, 2, Red)
