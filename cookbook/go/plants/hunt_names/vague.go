package plants

type Data struct { // want: vague
	info string // want: vague
	name string
}

func handleData(d Data) string { // want: vague
	data := d.info // want: vague
	return data
}

func process(stuff []string) int { // want: vague, vague
	for _, item := range stuff { // want: vague
		_ = item
	}
	return len(stuff)
}

func handle() {}

func run() {}

func parseNote(text string) (note string, err error) {
	return text, nil
}
