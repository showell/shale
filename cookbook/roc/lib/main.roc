## The modules the cookbook's Roc recipes share, a package each recipe's app
## names by a relative path (`lib: "../lib/main.roc"`, then `import lib.Tree`):
## Roc's tree of a file read (Tree), what a file declares (Declarations),
## text cut and ordered as Shale's builtins do it (Text), and a check's
## finding (Finding). None reaches an effect: a recipe's own modules do that,
## through `import pf.Shale` (roc_platform/README.md, "A script may be
## several files").
package [Declarations, Finding, Text, Tree] {}
