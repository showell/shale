## A Spec past the limit: tests have no limit, so the run leaves it out.
import Over

OverSpec :: [].{}

expect Over.one == 1

expect Over.two == 2

expect Over.three == 3

expect Over.four == 4

expect Over.five == 5

expect Over.six == 3

expect Over.one + Over.two == 3

expect Over.one + Over.three == 4

expect Over.two + Over.three == 5
