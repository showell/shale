module [classify, quoted]

## Names a byte: `c` is the byte, and 'c' is only a letter.
classify : U8 -> Str
classify = |c| {
    if c == 'c' {
        "the letter c"
    } else if c == '\'' or c == '"' {
        quoted(c)
    } else {
        c.to_str()
    }
}

quoted : U8 -> Str
quoted = |c| if c == '\'' "single ${c.to_str()}" else "double ${c.to_str()}"
