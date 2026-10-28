# Quiet for every check: an effect that takes an effectful function, its
# `!` naming the callback's effect, judged by its verb without the `!`.
# `map2` and `on` are no verbs, but `map2!` is `map2` run with an effect.
Try := [].{
	map2! : Try(a, err), Try(b, err), (a, b => c) => Try(c, err)
	map2! = |first, _second, transform!| first.map!(transform!)

	catch! : Try(ok, err), (err => a), (ok => a) => a
	catch! = |try, on_err!, _on_ok!| on_err!(try)
}

# Unannotated, its lambda's argument named with a `!`.
on_err! = |try, recover!| recover!(try)

# Annotated alone, as a hosted effect is.
Host := [].{
	each_line! : Str, (Str => {}) => {}
	pair_each! : (Str, U8 => {}) => {}
}
