app [main!] { pf: platform "../pf/main.roc" }

only_tests : F64
only_tests = 1.5

expect only_tests == 1.5
