# Quiet for every check: `_is_` and `_has_` names ask, whatever their first
# word (`unsigned` ends in -ed but is no state).
unsigned_is_even : U64 -> Bool
unsigned_is_even = |number| number % 2 == 0

unsigned_is_multiple_of : U64, U64 -> Bool
unsigned_is_multiple_of = |number, divisor| number % divisor == 0

signed_has_sign : I64 -> Bool
signed_has_sign = |number| number < 0
