## A file whose rewrite breaks `roc check` where no rewritten call is: with
## no annotation, `bytes.from_utf8()` checks here, as a method any argument
## may have, and the error is in Breaks.roc, which hands it a list. Judged
## on Breaks.roc (the self-test's `root`), the file is put back, and its
## call reported.
Decode := [].{
	decoded = |bytes| Str.from_utf8(bytes)
}
