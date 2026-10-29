## A plant for name_hunt's `container`: record fields named only by
## container words, in a type and in an annotation.
Page : { items : List(Str), title : Str }

listing : Str -> { entries : List(Str), skipped : U64 }
