app [step] { pf: platform "./platform/main.roc" }

# The platform chose `step`'s arguments, and the app chose `blend`'s.
step : U8, U8, U8 -> U8 # findings 0
step = |a, b, c| a + b + c # findings 0

blend : U8, U8, U8 -> U8 # findings 1 advice record
blend = |a, b, c| a + b + c
