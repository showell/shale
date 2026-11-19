# Quiet for every check: docs that cite another language's API, the source
# before the name in its sentence, on its line or the line above.

## A digit, as Rust's `is_ascii_digit` says, and as C's `is_digit_c` does.
## Zig `isDigit`, Rust `is_ascii_alphanumeric`, and wrapped, Zig `isHex`, Rust
## `is_ascii_hexdigit`, Python `string.punctuation`.
digit_width : U64
digit_width = 1
