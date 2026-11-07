# A client of the dead/ plants, read only for its calls: its production call
# keeps `QuietDeadClientCalled.by_client` alive, and its `expect` keeps
# nothing alive under `test_reach=dead`.
app [main] { pf: platform "../../plat/main.roc" }

import QuietDeadClientCalled
import DeadClientExpect

main = |x| QuietDeadClientCalled.by_client(x)

unchecked_leftover = |x| x

expect DeadClientExpect.only_in_client_expect(1) == 1
