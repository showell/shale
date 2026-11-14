# A plant for `phrase_name`: an empty union with no members holds no functions, so
# it makes no module of them.
Never : []

from_text : Str -> U32
from_text = |text| text.count_utf8_bytes()
