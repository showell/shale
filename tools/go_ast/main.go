// go-ast prints a Go file's syntax tree, as Go's own parser (go/parser)
// sees it, one fact a line for a program: Shale's `go_tree` reads it. The
// paths come on stdin, one a line; the one argument is --flat.
//
// The format is roc-ast's --flat (tools/roc_ast/main.zig), so one reader in
// Shale's host takes both. The first line says the format and whose parser:
// `go-ast flat 1 <go version>`; a change a reader must know of is a new
// number. Then, for each path, `== <path>` and its nodes:
//
//	n<TAB>parent<TAB>kind<TAB>line<TAB>col<TAB>end_line<TAB>end_col
//	a<TAB>node<TAB>name<TAB>value
//
// Nodes are numbered from 1 in each file, a node before its children, the
// children in the order go/ast declares its fields, which is source order.
// `parent` 0 is the file's. A kind is go/ast's type name (`FuncDecl`,
// `CallExpr`, `Ident`). Lines and columns count from 1, columns in bytes,
// and the end is exclusive; a node with no position has 0s. A value escapes
// `\`, tab, newline and carriage return as `\\`, `\t`, `\n`, `\r`.
//
// The attributes are the node's own scalars, by field name in lower case
// (an empty string left out):
// an Ident's `name`, a BasicLit's `value` (and its token kind as `lit`,
// since `kind` is the node's), an operator's `op` or `tok` (`+`, `:=`,
// `import`), a Comment's `text`. Every node but the file has `role`, the
// field of its parent that holds it (`Fun`, `Args`, `Body`, `Recv`,
// `Results`), so a reader need not guess which child is which.
//
// The resolver's objects and scopes are left out (the parser skips them),
// and so is File.Comments: a doc comment is in its declaration's `Doc`, and
// a plain comment between statements is in no tree.
//
// A file that does not parse cleanly is no tree: go-ast prints every tree
// before it, then `<path>: N parse problem(s), the first at L:C: <why>` on
// stderr, and exits 1. A file it cannot read is the same, `<path>: <why>`.
package main

import (
	"bufio"
	"errors"
	"fmt"
	"go/ast"
	"go/parser"
	"go/scanner"
	"go/token"
	"io"
	"os"
	"reflect"
	"runtime"
	"strings"
)

const flatFormat = 1

var nodeType = reflect.TypeFor[ast.Node]()

// The fields that are no part of the tree as written: the resolver's, and
// lists that repeat nodes the tree already holds.
var leftOut = map[string]bool{"Obj": true, "Scope": true, "Unresolved": true, "Imports": true, "Comments": true}

type flat struct {
	fset  *token.FileSet
	out   *bufio.Writer
	count int
}

func escaped(s string) string {
	return strings.NewReplacer(`\`, `\\`, "\t", `\t`, "\n", `\n`, "\r", `\r`).Replace(s)
}

func (w *flat) attr(at int, name, value string) {
	fmt.Fprintf(w.out, "a\t%d\t%s\t%s\n", at, name, escaped(value))
}

func (w *flat) node(n ast.Node, parent int, role string) {
	w.count++
	me := w.count
	var line, col, endLine, endCol int
	if n.Pos().IsValid() {
		p := w.fset.Position(n.Pos())
		line, col = p.Line, p.Column
	}
	if n.End().IsValid() {
		e := w.fset.Position(n.End())
		endLine, endCol = e.Line, e.Column
	}
	v := reflect.ValueOf(n).Elem()
	fmt.Fprintf(w.out, "n\t%d\t%s\t%d\t%d\t%d\t%d\n", parent, v.Type().Name(), line, col, endLine, endCol)
	if role != "" {
		w.attr(me, "role", role)
	}
	var kids []func()
	for i := 0; i < v.NumField(); i++ {
		f := v.Type().Field(i)
		fv := v.Field(i)
		if leftOut[f.Name] {
			continue
		}
		name := strings.ToLower(f.Name)
		switch {
		case f.Type == reflect.TypeFor[token.Token]():
			if name == "kind" {
				name = "lit"
			}
			w.attr(me, name, fv.Interface().(token.Token).String())
		case f.Type.Kind() == reflect.String:
			// An empty one is absent, as File's GoVersion is without a //go:build.
			if fv.String() != "" {
				w.attr(me, name, fv.String())
			}
		case f.Type.Implements(nodeType):
			if !fv.IsNil() {
				child := fv.Interface().(ast.Node)
				kids = append(kids, func() { w.node(child, me, f.Name) })
			}
		case f.Type.Kind() == reflect.Slice && f.Type.Elem().Implements(nodeType):
			for j := 0; j < fv.Len(); j++ {
				if !fv.Index(j).IsNil() {
					child := fv.Index(j).Interface().(ast.Node)
					kids = append(kids, func() { w.node(child, me, f.Name) })
				}
			}
		}
	}
	// A node's attributes all come before its first child's line.
	for _, k := range kids {
		k()
	}
}

func refuse(out *bufio.Writer, said string) {
	out.Flush()
	fmt.Fprintln(os.Stderr, said)
	os.Exit(1)
}

func main() {
	if len(os.Args) != 2 || os.Args[1] != "--flat" {
		fmt.Fprintln(os.Stderr, "go-ast: the paths come on stdin; the one argument is --flat")
		os.Exit(2)
	}
	list, err := io.ReadAll(os.Stdin)
	if err != nil {
		fmt.Fprintln(os.Stderr, "go-ast: stdin:", err)
		os.Exit(2)
	}
	out := bufio.NewWriterSize(os.Stdout, 64*1024)
	fmt.Fprintf(out, "go-ast flat %d %s\n", flatFormat, runtime.Version())
	for _, raw := range strings.Split(string(list), "\n") {
		path := strings.TrimSpace(raw)
		if path == "" {
			continue
		}
		source, err := os.ReadFile(path)
		if err != nil {
			var pe *os.PathError
			if errors.As(err, &pe) {
				err = pe.Err
			}
			refuse(out, fmt.Sprintf("%s: %v", path, err))
		}
		fset := token.NewFileSet()
		file, err := parser.ParseFile(fset, path, source, parser.ParseComments|parser.SkipObjectResolution)
		if err != nil {
			var problems scanner.ErrorList
			if errors.As(err, &problems) && len(problems) > 0 {
				first := problems[0]
				refuse(out, fmt.Sprintf("%s: %d parse problem(s), the first at %d:%d: %s", path, len(problems), first.Pos.Line, first.Pos.Column, first.Msg))
			}
			refuse(out, fmt.Sprintf("%s: %v", path, err))
		}
		fmt.Fprintf(out, "== %s\n", path)
		w := &flat{fset: fset, out: out}
		w.node(file, 0, "")
	}
	out.Flush()
}
