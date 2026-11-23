module [rect, clamp]

# dictated.txt names `rect`, whose bare arguments copy another library's API,
# so they are no finding, and nor are its lambda's. `clamp` is not named
# there, so its arguments are.
rect : F32, F32, F32, F32 -> F32 # findings 0
rect = |x, y, width, height| x + y + width + height

clamp : F32, F32, F32 -> F32 # findings 1 advice record
clamp = |v, lo, hi| v + lo + hi
