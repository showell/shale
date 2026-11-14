# A plant for boundaries' `constant`, quiet: `{}` beside another parameter
# leaves a function of that one.
padded : {}, Str -> Str
padded = |{}, s| Str.concat(s, " ")
