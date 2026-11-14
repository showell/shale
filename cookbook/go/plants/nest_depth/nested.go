package plants

import (
	"strings"
	"unicode/utf8"
)

func title(s string) int {
	return utf8.RuneCountInString(strings.TrimSpace(strings.ToLower(s))) // want: nested 3
}

func four(s string) int {
	return len(strings.Fields(strings.TrimSpace(strings.ToLower(s)))) // want: nested 4
}

func spread(s string) string {
	return strings.Repeat( // want: nested 3
		strings.TrimSpace(
			strings.ToLower(s),
		),
		2,
	)
}

func twoOnOneLine(a, b string) []string {
	return []string{strings.ToUpper(strings.TrimSpace(strings.ToLower(a))), strings.ToUpper(strings.TrimSpace(strings.ToLower(b)))} // want: nested 3, nested 3
}

func throughOperators(s string) int {
	return len(strings.TrimSpace(strings.ToLower(s)) + "!") // want: nested 3
}

func throughIndex(s string) int {
	return len(strings.Fields(strings.ToLower(s))[0]) // want: nested 3
}
