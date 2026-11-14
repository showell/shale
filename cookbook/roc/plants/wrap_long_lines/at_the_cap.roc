module [deep]

# Seven groups, one a pass, so the eighth pass changes nothing.
deep = f(a, b, f(a, b, f(a, b, f(a, b, f(a, b, f(a, b, f(a, b, c, d, e, g, h, i)))))))
