## Quiet for `worker_name`: a `_go` that does not call itself is the verb
## (a piece that can go n spaces), not a recursion's worker.
can_go : U64, U64 -> Bool
can_go = |n, room| n <= room
