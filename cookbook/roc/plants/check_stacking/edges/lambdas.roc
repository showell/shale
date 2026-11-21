module [piped, ored, applied, opened, in_record, after_colon]

piped = |x| x |> f(a, b, c, d, e, g)
ored = |a, b| a or f(a, b, c, d, e, g)
applied = List.map(xs, |a, b, c| a + b + c + d + e)
opened = [|a, b, c| a, |d, e, g| d]
in_record = { run: |a, b, c| a + b, d: 1 }
after_colon = |p| match p {
	A => |a, b, c, d| a
}
