package plants

type options struct {
	noCache bool // want: negative
	notes   bool
}

func render(text string, noEscape bool) string { // want: negative
	return text
}

func isNotEmpty(text string) bool { // want: negative
	return text != ""
}

var notReady bool // want: negative

func show(nothing int, notice bool) int { return nothing }
