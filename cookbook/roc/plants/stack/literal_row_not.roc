module [mixed, called]

# Not a row of literals: a name among the items, a call's arguments (even
# all literals), and a string that interpolates each stack.
mixed = |a| [a, 0, 0, 0, 0, 0] # findings 1

called = Num.max6(1, 2, 3, 4, 5, 6) # findings 1

spliced = |n| ["a", "${n}", "c", "d", "e", "f"] # findings 1
