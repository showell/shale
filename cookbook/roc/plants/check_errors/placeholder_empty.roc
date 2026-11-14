# A plant for errors' `placeholder`: an empty string is a literal too.
color_of : List(Str), U64 -> Str
color_of = |colors, seat| List.get(colors, seat) ?? ""
