## Each import is used through a qualified value, a package's module, or a
## name qualified twice.
import game.Distance
import pf.Stdout
import Local

QuietQualified :: [].{
	far = Distance.metres(5)

	say! = |text| Stdout.line!(text)

	made = Local.Thing.make
}
