# A plant for `phrase_name`: a field that shares the file's name is no type.
from_seconds : U64 -> { phrase_name_from_field_name : U64 }
from_seconds = |seconds| { phrase_name_from_field_name: seconds }
