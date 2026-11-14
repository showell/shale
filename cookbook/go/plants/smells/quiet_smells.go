package plants

import "strings"

type style int

const (
	plain style = iota
	bold
)

// Short, shallow, a named type in place of a bool: no finding.
func label(text string, s style) string {
	if s == bold {
		return "<b>" + text + "</b>"
	}
	return text
}

func words(lines []string) int {
	n := 0
	for _, l := range lines {
		if l == "" {
			continue
		}
		n += len(strings.Fields(l))
	}
	return n
}
