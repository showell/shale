# A plant for boundaries' `constant`: a plain comment is no `##` doc, so it
# doesn't answer.

# The platform calls it once a frame.
commented_board : {} -> List(U8)
commented_board = |{}| [1, 2, 3]
