## LastSegment's helper: a type module whose nested types are named for
## modules, `Shelf.Book` for Book.roc, as fasttrack's `Type.Game` is for
## Game.roc, and `Shelf.Shelf` for Shelf itself, as its `History.History`.
Shelf :: [].{
	Book := { title : Str }.{
		named : Str -> Book
		named = |book_title| { title: book_title }

		title : Book -> Str
		title = |b| b.title
	}

	Shelf : { books : List(Book) }

	count : Shelf.Shelf -> U64
	count = |s| s.books.len()
}
