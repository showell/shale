## A package module that imports its own package's module, as book_builder's
## builder imports roc-markdown's `md`.
import inner.Inner

Outer := [].{
	greet : Str -> Str
	greet = |name| Inner.shout("hello ${name}")
}
