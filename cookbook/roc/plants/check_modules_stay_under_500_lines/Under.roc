## At the self-test's limit of 20 lines, its expect included: left alone.
Under :: [].{}

one : U64
one = 1

two : U64
two = 2

three : U64
three = 3

four : U64
four = 4

five : U64
five = 5

# Its expect counts, as every line does.
expect one + four == five
