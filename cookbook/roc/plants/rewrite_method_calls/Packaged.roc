## Under the types (root PkgApp): `m`'s type is Meter, the type module of
## the package `pk` (PlantPkg), imported as `pk.Meter`. roc-types writes a
## type by its own module's name, never its package's, so the type's head is
## `Meter`, the module called, and the call is proven: a package-qualified
## real match.
import pk.Meter

Packaged :: [].{
	twice : Meter -> Meter
	twice = |m| Meter.scaled(m, 2)
}

expect Packaged.twice(Meter.two).count() == 4
