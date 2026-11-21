## The splitter the recipes that read Shale's C share: a file of Shale's C
## split into its top-level items, from the dialect c/check_discipline.shale
## holds. A recipe names it by a relative path in its app's header (`items:
## "../items/main.roc"`, then `import items.Split`). It reaches no effect:
## a recipe reads the file and hands Split.items its text.
##
##   Split   the splitter, and Shale's `lines`
##   Guard   a header's guard, framed
##   Item    an item and its kinds
##   Mask    the masker, and the byte classes the readers read by
##   Head    the readers of an item's opening line or head
##   Words   the names in an item's text: its uses, an enum's constants
package [Guard, Head, Item, Mask, Split, Words] {}
