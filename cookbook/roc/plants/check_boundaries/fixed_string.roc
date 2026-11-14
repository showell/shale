# A plant for boundaries' `fixed`: the same string at both calls.
labelled : Str, Str -> Str
labelled = |label, s| Str.concat(label, s)

note : Str -> Str
note = |s| labelled("note: ", s)

remark : Str -> Str
remark = |t| labelled("note: ", t)
