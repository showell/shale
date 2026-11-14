module [walk, walked]

# A run of Bools a caller passes: the advice is a tag, as the guide says
# of a flag (`advice flag`: each finding on the line says `flag`), where
# other bare arguments are told to take a record.
walk : Bool, Bool, Bool -> U8 # findings 1 advice flag
walk = |_, _, _| 0

walked = walk(Bool.true, Bool.false, Bool.true) # findings 1 advice flag

mixed : Bool, U8, Bool -> U8 # findings 1 advice record
mixed = |_, _, _| 0
