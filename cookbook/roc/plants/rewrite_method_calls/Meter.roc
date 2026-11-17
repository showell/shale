## PlantPkg's one module, a type module.
Meter := { n : U64 }.{
	two : Meter
	two = { n: 2 }

	add : Meter, Meter -> Meter
	add = |a, b| { n: a.n + b.n }

	count : Meter -> U64
	count = |m| m.n
}
