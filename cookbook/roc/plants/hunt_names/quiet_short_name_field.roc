# Quiet for `short_name`: a point's coordinates, a colour's channels, a
# triangle's corners, and a field of two letters.
Point : { x : F64, y : F64, z : F64, w : F64 }

Rgba : { r : F64, g : F64, b : F64, a : F64 }

Triangle : { a : Point, b : Point, c : Point }

Move : { at : U64, stop : U64 }
