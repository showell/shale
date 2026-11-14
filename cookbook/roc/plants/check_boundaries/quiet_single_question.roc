# Quiet for boundaries' `single`: a question, whose name is the concept its
# test stands for.
is_blank : Str -> Bool
is_blank = |s| Str.trim(s) == ""

kept : List(Str) -> List(Str)
kept = |ls| List.drop_if(ls, |l| is_blank(l))
