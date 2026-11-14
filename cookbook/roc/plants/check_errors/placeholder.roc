# A plant for errors' `placeholder`: a lookup that can't fail, its failure
# hidden as a value (FastTrack's Color.roc).
color_of : List(Str), U64 -> Str
color_of = |colors, seat| List.get(colors, seat) ?? "bogus"
