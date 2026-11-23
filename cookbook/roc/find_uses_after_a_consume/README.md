# find_uses_after_a_consume

A checker for Roc's "copy cliffs": places where a write that should be O(1)
quietly becomes O(N).

## The problem it looks for

Roc's values are immutable, but a write such as `List.set`, `List.append`,
`Str.concat` or `Dict.insert` updates the value in place when, at run time,
nothing else refers to it. When something does, the write copies the whole
value first. Nothing in the types or the compiler's output says which
happened, so one innocent line can turn a fast loop slow:

```roc
ys = xs.set(j, i) ?? xs
v = xs.get(j) ?? crash "in range"   # xs is still alive, so the set above copied
```

Every copy we could produce on Roc 0.1.0-RC1 has the same shape: **something
uses the old value after a write has consumed it.** This recipe looks for
that shape statically, reading the code through Roc's own parser (roc-ast)
and, optionally, its types (roc-types).

## What it checks

It walks each function and each lambda in evaluation order. It tracks every
value by path (`xs`, `st.items`), takes both branches of an `if` or `match`,
and walks each loop body twice. There are five checks:

| check | what it finds |
|---|---|
| `after_consume` | a value read after a consuming call took it, or taken twice |
| `whole_record` | a record read whole after, or inside, the write of one of its fields |
| `closure` | a closure called after a write took a value it holds, or a closure that writes a value its maker still holds |
| `held` | a value got from a container (`get`, `first`, `last`, a `match`) and written while the container is still used |
| `restored_refcounted` | with `types=1`, `?? xs` handing back a list whose entries hold reference counts (`List(Str)`) |

Which builtins consume their argument is a table in `Consume.roc`, taken
from the builtins RC1's compiler checks for uniqueness at run time. The
table's comments name them. Two rules keep it from crying wolf:

- `xs.set(i, v) ?? xs` names `xs` again only on the error path, where RC1
  hands the list back, so it isn't a use.
- `sublist` and the drops and takes aren't consumers: they slice a shared
  list without copying.

A finding prints as `path:line: check: why`.

## Running it

```
cd <repo> && shale <shale>/cookbook/roc/find_uses_after_a_consume.shale glob='basic/roc/*.roc'
cd <repo> && shale <shale>/cookbook/roc/find_uses_after_a_consume.shale glob='app/*.roc' types=1 root=app/main.roc
shale cookbook:roc/find_uses_after_a_consume.shale self_test=1
```

`self_test=1` runs the checks over the plants in
`cookbook/roc/plants/find_uses_after_a_consume/`. Each plant file holds pairs of
functions that differ by one line, one writing in place and one copying,
taken from roc-apps' copy shapes. The copying member's marked line must be
found, and the in-place member must stay clean.

## How we know how good it is

Its findings are checked against what actually happens at run time.
roc-apps holds 36 copy shapes in
[tools/copy_shapes](https://github.com/showell/roc-apps/tree/master/tools/copy_shapes),
each counted by allocation in release, dev and boxy builds, and an oracle
([count_copies.c](https://github.com/showell/roc-apps/blob/master/tools/count_copies.c))
that catches real copies with breakpoints on the builtins and names the Roc
function that made each one.

Scored against that oracle on 2026-11-23:

| workload | build | precision | recall |
|---|---|---|---|
| the copy shapes | release | 6/10 | 6/6 |
| the copy shapes | dev | 9/10 | 9/9 |
| the copy shapes | boxy | 9/10 | 9/11 |
| BASIC's controls | dev | 1/1 | 1 of 7 functions |

## Limitations

- **One function at a time.** A call to the program's own function consumes
  nothing in its model. Most real copies it misses cross a function
  boundary: a caller still holds a value that a helper writes. That's why
  BASIC's recall is 1 of 7. The next rule is per-function summaries (does a
  function only read an argument, consume it, or hand it back), inferred
  rather than written by hand.
- **"May copy", not "copies".** It models the dev build, where every function
  keeps its own signature. A release build inlines and specializes before
  it decides on reference counts, so some findings write in place there.
  Several shapes copy only in dev builds, and roc-apps' notes list them.
- **It can't tell `len` from `get`.** A list's length read after its write
  doesn't copy on RC1, but a `get` there does. The checker reports both. The
  plant `over_reports.roc` records this on purpose.
- **What it can't see:** the inside of Builtin's own Dict code (two boxy-only
  copies come from there), opaque types from packages whose source it
  doesn't read, and anything a host does with a value between calls.
- **Report mode only.** No gate holds its count to zero yet. On BASIC and the
  JS interpreter it's a list for a person to read, not a rule.

## Where it's going

The aim, from Steve's question about Roc's copy cliffs, is a check that
finds the shape before it ships, without changing Roc. That means a lint
outside the compiler, since Roc has no annotations for uniqueness. roc-apps
also has an opt-in opaque marker, `Unique(a) :: a.{ ... }` in its copy
shapes, that this checker recognizes, so code can say which values must
never be copied. The cheapest help Roc itself could give is a dev-mode
report of each checked write that took its copy path, which is what the
oracle does from outside today.
