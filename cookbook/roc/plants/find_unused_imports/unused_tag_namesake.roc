module [mode]

import game.Other

# The tag `Other` shares the module's name, which is no use of the module.
mode : U8 -> [Other, Plain]
mode = |n| if (n > 0) Other else Plain
