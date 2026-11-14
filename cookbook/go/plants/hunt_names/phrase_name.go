package plants

type Note struct{ text string }

func inView(notes []Note) []Note { return notes } // want: phrase_name

func byName(name string) string { return name } // want: phrase_name

func afterSave() {} // want: phrase_name

func toJSON(n Note) string { return n.text }

func fromText(text string) Note { return Note{text: text} }

func withText(n Note, text string) *Note { return &Note{text: text} }

func inDraft(n Note) bool { return n.text == "" }

func notesInView(notes []Note) []Note { return notes }
