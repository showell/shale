import inner.Inner

Outer := [].{
	greet : Str -> Str
	greet = |name| Inner.shout("hello ${name}")
}
