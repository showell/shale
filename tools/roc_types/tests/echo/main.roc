## An app that names no platform runs on the Echo platform, whose `echo!` is
## in scope: roc-types must check it as `roc check` does.
app [main!] {}

main! = |_args| {
	echo!(Str.concat("hello", " echo"))
	Ok({})
}
