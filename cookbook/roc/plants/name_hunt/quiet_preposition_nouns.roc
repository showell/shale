## Quiet for `stacked_preposition`: a name made only of prepositions uses
## its first as a noun (a message's `at`, a grant's `via`), so no worker's
## suffix is stacked on an entry's.
at_of : { at : U64 } -> U64
at_of = |m| m.at

via_from : { via : Str } -> Str
via_from = |g| g.via
