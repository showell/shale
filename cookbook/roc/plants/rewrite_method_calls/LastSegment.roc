## Under the types (root TypedApp): `b`'s type is `Shelf.Book`, a type nested
## in Shelf whose last segment names the module Book, as fasttrack's
## `Type.Game` names Game; `s`'s is `Shelf.Shelf`, nested in Shelf under
## Shelf's own name, as its `History.History`. A method is looked up in the
## type's own block, which holds neither function, so the compiler refuses
## both as method calls (`missing method`, the LastSegmentTried plant), and
## the types prove neither: each is left, its type the reason.
import Shelf
import Book

LastSegment :: [].{
	label : Shelf.Book -> Str
	label = |b| Book.heading(b)

	size : Shelf.Shelf -> U64
	size = |s| Shelf.count(s)
}

expect LastSegment.label(Shelf.Book.named("Emma")) == "Emma:"
expect LastSegment.size({ books: [] }) == 0
