module [deep]

# Eight groups, one a pass, so the eighth pass still changes something.
deep = f(a, b, f(a, b, f(a, b, f(a, b, f(a, b, f(a, b, f(a, b, f(a, b, c, d, e, g, h, i))))))))
