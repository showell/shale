## Decode.roc's caller: its own call, a list's, is no method of a list.
import Decode

Breaks := [].{}

expect Decode.decoded([97]) == Ok("a")
