# Quiet for errors' `placeholder`: a fallback that is no string literal: a
# crash with a reason, a value of the program's, a string built from one.
color_of : List(Str), U64 -> Str
color_of = |colors, seat| List.get(colors, seat) ?? crash "color_of: a seat past the players"

name_of : List(Str), Str, U64 -> Str
name_of = |names, guest, i| List.get(names, i) ?? guest

label_of : List(Str), U64 -> Str
label_of = |labels, i| List.get(labels, i) ?? "seat ${U64.to_str(i)}"
