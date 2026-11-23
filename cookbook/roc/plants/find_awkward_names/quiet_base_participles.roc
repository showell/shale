# Quiet for every check: a participle that is its own base form names the
# act (`read!` reads), so cookbook/words/participles.txt leaves it out.
read_all! : Str => Str
read_all! = |path| path

set_flag! : Str => Str
set_flag! = |flag| flag

put_line! : Str => Str
put_line! = |line| line

split_lines! : Str => Str
split_lines! = |text| text
