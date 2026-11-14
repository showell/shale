package plants

import (
	"fmt"
	"strings"
)

type note struct{ text string }

// Each of these reads left to right, or starts its count again: no finding.
func chain(b *strings.Builder, s string) string {
	b.WriteString(strings.ToLower(s))
	return strings.NewReplacer("a", "b").Replace(strings.TrimSpace(s))
}

func conversions(b []byte) string {
	return string([]byte(string(b)))
}

func twoDeep(s string) int {
	return len(strings.TrimSpace(s))
}

func literals(s string) []note {
	return append([]note{}, note{text: strings.ToLower(fmt.Sprint(s))})
}

func lambdas(s string) func() string {
	return func() string { return strings.ToLower(strings.TrimSpace(s)) }
}

func named(s string) string {
	lower := strings.ToLower(s)
	return strings.Repeat(strings.TrimSpace(lower), 2)
}
