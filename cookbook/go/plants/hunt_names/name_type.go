package plants

type page struct {
	lineCount string // want: name_type
	wordCount int
	notePath  []byte // want: name_type
	rootPath  string
}

func save(notePath int, byteCount uint64) {} // want: name_type

func open(fsPath FilePath, lineCount int) {}

type FilePath string
