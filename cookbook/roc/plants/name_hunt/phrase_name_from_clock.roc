# A plant for `phrase_name`: `from_clock` neither takes nor gives `DateTime`, the
# type its module has, as `from_str` beside it does.
DateTime := Str.{
	from_str : Str -> Try(DateTime, [WrongShape])
	from_str = |text| Ok(DateTime.(text))
}

from_clock : I64 -> Try(Str, [OutOfRange])
from_clock = |seconds| Ok(seconds.to_str())
