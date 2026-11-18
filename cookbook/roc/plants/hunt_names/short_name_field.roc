# A plant for hunt_names's `short_name`: a record field of one letter, in a
# type and in an annotation, and a channel's or a coordinate's letter with
# no set beside it.
Move : { a : U64, stop : U64 }

move_total : { k : U64, stop : U64 } -> U64
move_total = |{ k, stop }| k + stop

Size : { w : F64, h : F64 }
