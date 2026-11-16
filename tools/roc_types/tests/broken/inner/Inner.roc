## A type error in the package the app reaches only through `outer`: roc-types
## must refuse the app, as `roc check` does, and write no module.
Inner := [].{
	shout : Str -> Str
	shout = |text| text.concat(1)
}
