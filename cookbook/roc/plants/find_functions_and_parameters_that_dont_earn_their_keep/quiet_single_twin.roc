# The twin of quiet_single_twinned.roc: a test file, whose reference is no
# production caller's.
twin_upper : Str -> Str
twin_upper = |s| Twinned.upper(s)
