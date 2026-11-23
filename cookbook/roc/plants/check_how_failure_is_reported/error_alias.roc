# A plant for errors' `error_alias`: a result's error named by a shared
# alias, not listed in place.
ReadError : [Missing, Failed, NotUtf8]

read_text : List(U8) -> Try(Str, ReadError)
read_text = |bytes| Str.from_utf8(bytes) ?? Err(NotUtf8)
