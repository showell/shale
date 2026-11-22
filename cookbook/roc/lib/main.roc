## The modules the cookbook's Roc recipes share, a package each recipe's app
## names by a relative path (`lib: "../lib/main.roc"`, then `import lib.Tree`):
## Roc's tree of a file read (Tree), several files' trees joined (Forest),
## what a file declares (Declarations), what its header dictates (Headers),
## the imports it never uses (Imports, find_unused_imports' rule), text cut
## and ordered as Shale's builtins do it (Text) and a line's bytes as they
## read it (Bytes), a check's finding (Finding), roc-types' output read
## (RocTypes), and a file's bracket groups (Groups) judged by the stacking
## rule (Stacking), the packed ones apart (Packed). None reaches an
## effect: a recipe's own modules
## do that, through `import pf.Shale` (roc_platform/README.md, "A script may
## be several files").
package [Bytes, Declarations, Finding, Forest, Groups, Headers, Imports, Packed, RocTypes, Stacking, Text, Tree] {}
