# A plant for `singular_list`: a function that gives another list than the
# one it takes makes something new, and a changed copy named by one word that
# is the element's own reads as one element.
first_row : List(Cell) -> List(Row)
first_row = |cells| []

cell : List(Cell) -> List(Cell)
cell = |cells| cells
