# Edges for check_boundaries' `##` docs: one above an annotation of several
# lines, one a blank line cuts off, a plain comment, and a doc above a
# constant's annotation.
## Its doc stands above an annotation of three lines.
spread :
	Str
	-> Str
spread = |s| Str.trim(s)

## A blank line stands between this doc and its function.

parted : Str -> Str
parted = |s| Str.trim(s)

# A plain comment, no doc.
plain : Str -> Str
plain = |s| Str.trim(s)

uses = |s| {
	a = spread(s)
	b = parted(a)
	plain(b)
}

## Made fresh at each call, on purpose.
fresh : {} -> List(U8)
fresh = |{}| [1, 2]

## A doc above the function, its annotation left out.
noted = |{}| [3]
