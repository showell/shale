# Plants for `whole_record`: a helper that reads the whole record inside
# the write of its field (copy-shapes' `helper-reads-record`, BASIC's
# Devices.poked, a copy in the dev and boxy builds), beside the index bound
# on the line before and an index that needs no record, both in place, and
# the record read whole after the write.
Screen : { cells : List(U64), base : U64 }

kept : Screen, U64 -> U64
kept = |d, a| (a + d.base) % d.cells.len()

index_computed_first : Screen, U64, U64 -> Screen
index_computed_first = |d, j, i| { ..d, cells: d.cells.set(j, i) ?? crash "index_computed_first: in range" }

index_bound_before : Screen, U64, U64 -> Screen
index_bound_before = |d, j, i| {
	k = kept(d, j)
	{ ..d, cells: d.cells.set(k, i) ?? crash "index_bound_before: in range" }
}

helper_reads_record : Screen, U64, U64 -> Screen
helper_reads_record = |d, j, i| { ..d, cells: d.cells.set(kept(d, j), i) ?? crash "helper_reads_record: in range" } # whole_record

read_whole_after : Screen, U64, U64 -> (List(U64), U64)
read_whole_after = |d, j, i| {
	cells = d.cells.set(j, i) ?? crash "read_whole_after: in range"
	(cells, kept(d, j)) # whole_record
}

sibling_after : Screen, U64, U64 -> (List(U64), U64)
sibling_after = |d, j, i| {
	cells = d.cells.set(j, i) ?? crash "sibling_after: in range"
	(cells, d.base)
}
