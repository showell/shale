## A note, which holds marks that may point back at a note.
Note :: { marks : List(Note.Mark) }.{

	## A mark in a note.
	Mark :: [Plain, Ref(Note)]
}
