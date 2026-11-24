module [count, kind, bump, Tally]

## A tally: `n` is the count so far, and `n_seen` a field beside it.
Tally : { n : U64, n_seen : Bool }

## Counts the newlines and the letter n: 'n' and '\n' are bytes, not names.
count : Str -> U64
count = |s| {
    var n = 0
    for b in s.to_utf8() {
        if b == '\n' or b == 'n' {
            n = n + 1
        }
    }
    n
}

kind : U8 -> Str
kind = |n| match n {
    '\n' => "newline"
    'n' => "the letter n"
    _ => "other"
}

## A spread takes the whole record: `..n` is a use, and `n:` and `.n` are
## the field's, left without fields=1.
bump : Tally -> Tally
bump = |n| { ..n, n: n.n + 1 }
