package plants

func isReady(n int) int { // want: question
	return n
}

func hasNotes() {} // want: question

func loaded(path string) bool { // want: question
	return path != ""
}

func isOpen(n int) bool {
	return n > 0
}

func hasNote(name string) (bool, error) {
	return name != "", nil
}

func canWrite() bool { return true }

func load(path string) string { return path }
