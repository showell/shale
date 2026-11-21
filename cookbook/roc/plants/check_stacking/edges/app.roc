app [main!, step] { pf: platform "./platform/main.roc" }

# The platform chose these names' arguments.
main! : U8, U8, U8 => {}
main! = |a, b, c| {}

step : U8, U8, U8 -> U8
step = |a, b, c| a

other : U8, U8, U8 -> U8
other = |a, b, c| a
