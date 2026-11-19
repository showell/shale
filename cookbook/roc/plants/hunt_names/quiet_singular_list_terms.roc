# Quiet for every check: a project's terms (terms.txt here). A `List(Stop)`
# is a route, and a term alone names an act, as a verb does.
route_at : List(List(Stop)), U64 -> List(Stop)
route_at = |routes, index| []

kept_route : List(Stop)
kept_route = []

Truck : { route : List(Stop), load : U64 }

two_opt : U64 -> List(Stop)
two_opt = |number| []
