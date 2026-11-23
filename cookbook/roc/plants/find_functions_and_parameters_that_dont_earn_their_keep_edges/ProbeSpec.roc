# Edges for find_functions_and_parameters_that_dont_earn_their_keep: a spec app is a test file, whose calls are a
# test's and whose own functions are never judged.
probe = |{}| padded("y", 3)

same = |c, a| if c a else a

lonely_probe = |x| x
