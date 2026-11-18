## PlantPkg's one module, a type module.
Meter := { n : U64 }.{
	two : Meter
	two = { n: 2 }

	scaled : Meter, U64 -> Meter
	scaled = |m, k| { n: m.n * k }

	count : Meter -> U64
	count = |m| m.n
}
