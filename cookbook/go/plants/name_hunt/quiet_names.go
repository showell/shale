package plants

import "strings"

// A file a careful Go programmer might write: no finding.
type Comment struct {
	Anchor  string
	Body    string
	replies int
}

func parseComments(text string) []Comment {
	var comments []Comment
	for _, line := range strings.Split(text, "\n") {
		if line == "" {
			continue
		}
		comments = append(comments, Comment{Body: line})
	}
	return comments
}

func (c Comment) isEmpty() bool { return c.Body == "" }

func newComment(body string) Comment { return Comment{Body: body} }
