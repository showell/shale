# Edges for check_boundaries: a client, read only for its calls.
app [main] { pf: platform "../typed/EdgePf.roc" }

main = Exposed.shown(Private.hidden(Kit.unused_tool(1)))

client_only = |c, a| if c a else a
