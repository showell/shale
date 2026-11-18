# Quiet for every check: a function that gives its first argument's type is a
# changed copy, whatever its first word: a noun, an adjective, or a verb
# verbs.txt doesn't hold.
first_row : List(Cell) -> List(Cell)
first_row = |cells| cells

coalesce : List(Stop) -> List(Stop)
coalesce = |stops| stops

reinsert : List(Stop), { at : U64, stop : Stop } -> List(Stop)
reinsert = |stops, { at, stop }| stops.insert(at, stop)

stable_by_load : List(a), (a -> U64) -> List(a)
stable_by_load = |items, load_of| items
