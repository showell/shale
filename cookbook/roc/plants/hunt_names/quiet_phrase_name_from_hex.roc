# Quiet for every check: a module with no type of its own, whose `from_hex`
# mirrors `to_hex`, and the worker beside it.
Hex :: [].{
	to_hex : List(U8) -> Str
	to_hex = |bytes| Str.from_utf8(bytes) ?? ""

	from_hex : Str -> Try(List(U8), [NotHex])
	from_hex = |text| from_hex_onto({ out: [], rest: text.to_utf8() })
}

from_hex_onto : { out : List(U8), rest : List(U8) } -> Try(List(U8), [NotHex])
from_hex_onto = |{ out }| Ok(out)
