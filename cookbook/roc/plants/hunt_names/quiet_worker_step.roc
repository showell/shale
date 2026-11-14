## Quiet for `worker_name`: a `_step` or `_walk` that does not call itself
## is a fold's step or a real walk, not a recursion's worker.
physics_step : U64, U8 -> U64
physics_step = |total, x| total + U8.to_u64(x)

sum_walk : List(U8) -> U64
sum_walk = |xs| List.walk(xs, 0, physics_step)
