# A plant for name_hunt's `noun_effect`: beside `spend_nonce!`, which
# spends one, an effect named for the thing alone still reads as a value.
spend_nonce! : Str => Str
spend_nonce! = |nonce| nonce

nonce! : Str => Str
nonce! = |seed| seed
