module [shown]

## A `module [...]` file's top level can be reached from outside, so its
## helpers are judged.
shown : U8
shown = 1

only_tests : F64
only_tests = 1.5

expect only_tests == 1.5
