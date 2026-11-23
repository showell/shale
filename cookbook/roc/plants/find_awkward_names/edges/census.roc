# Edges for the census of effects' first words: ties broken by the word, a
# word counted once a name however often it is annotated and defined, and
# every verdict.
read_one! : U64 => U64
read_one! = |number| number

read_two! : U64 => U64

write_one! : U64 => U64

write_two! : U64 => U64

held_one! : U64 => U64

done! : {} => {}

process! : {} => {}

data! : {} => {}

is_open! : Str => Bool

has_room! : Str => Str

links! : {} => {}

apple! : {} => {}

zebra! : {} => {}
