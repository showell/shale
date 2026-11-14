package plants

import "strings"

func render(lines []string) string { // want: long_func
	var b strings.Builder
	for _, l := range lines {
		b.WriteString(l)
		b.WriteString("\n")
	}
	if b.Len() == 0 {
		return ""
	}
	if b.Len() > 1000 {
		return "too long"
	}
	if strings.HasPrefix(b.String(), "#") {
		return "a heading"
	}
	out := b.String()
	out = strings.TrimSpace(out)
	return out
}

func count(lines []string) int {
	n := 0
	for _, l := range lines {
		if l != "" {
			for _, c := range l {
				switch c { // want: deep_block
				case 'a':
					if n > 3 {
						n++
					}
				}
			}
		}
	}
	return n
}

func chain(n int) int {
	if n == 0 {
		return 0
	} else if n == 1 {
		return 1
	} else if n == 2 {
		for i := 0; i < n; i++ {
			if i > 1 {
				return i
			}
		}
	}
	return n
}

func literal(lines []string) func() int {
	for range lines {
		for range lines {
			for range lines {
				return func() int {
					if len(lines) > 0 {
						return 1
					}
					return 0
				}
			}
		}
	}
	return nil
}

func place(x, y, w, h int, label, style string) {} // want: many_params

func five(x, y, w, h int, label string) {}

func draw(text string, bold bool, italic bool) {} // want: bool_param

func show(text string, wide int) {}
