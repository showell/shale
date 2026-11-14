# A plant for `phrase_name`: `from_*` that neither takes nor gives its own
# module's type is no conversion. Two prepositions are no noun either.
from_behind : Str -> List(Shapes.Shape)
from_behind = |text| Shapes.parse(text)
