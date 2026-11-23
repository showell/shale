# Quiet plants: a list in a record written every way RC1 writes in place
# (copy-shapes' `record-rebuilt`, `record-length-before-set`,
# `record-update`, `field-inside-update`, `field-taken-out-first`), and
# BASIC's Vec.set_in, a node taken out (`replace`) before it is written.
Totals : { items : List(U64), total : U64 }

record_rebuilt : Totals, U64, U64 -> Totals
record_rebuilt = |totals, j, i| { items: totals.items.set(j, i) ?? totals.items, total: totals.total + 1 }

record_length_before_set : Totals, U64, U64 -> Totals
record_length_before_set = |totals, j, i| { total: totals.total + totals.items.len(), items: totals.items.set(j, i) ?? totals.items }

record_update : Totals, U64, U64 -> Totals
record_update = |totals, j, i| { ..totals, items: totals.items.set(j, i) ?? totals.items }

field_taken_out_first : Totals, U64, U64 -> Totals
field_taken_out_first = |totals, j, i| {
	items = totals.items
	{ ..totals, items: items.set(j, i) ?? crash "field_taken_out_first: in range" }
}

Node : [Leaf(List(U64)), Branch({ list : List(Node), prev : Node })]

set_in : List(Node), U64, Node -> List(Node)
set_in = |ks, c, v| {
	taken = ks.replace(c, Leaf([])) ?? crash "set_in: in range"
	taken.list.set(c, merged(taken.value, v)) ?? crash "set_in: in range"
}

merged : Node, Node -> Node
merged = |_old, v| v
