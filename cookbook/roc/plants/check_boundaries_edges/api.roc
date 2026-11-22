module [exposed_fn, used_fn]

# Edges for check_boundaries: a `module` header exposes two names, so the one
# nothing calls is `unused_api`, and a helper nothing calls is `dead`.
exposed_fn = |x| x

used_fn = |x| x

inner = |x| used_fn(x)
