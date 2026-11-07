# A gate over the dead/ plants, read as a test: its call to
# `DeadGateCalled.only_gate_calls` is a test's, while its call to its own
# helper is production, so it gives no finding itself.
app [main] { pf: platform "../../plat/main.roc" }

import DeadGateCalled

main = |x| gate_helper(DeadGateCalled.only_gate_calls(x))

## The gate's own helper.
gate_helper = |x| x
