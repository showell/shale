## The import exposes a name the file never uses, and never names the module.
import pf.Stdout exposing [line!]

UnusedExposing :: [].{
	say = |text| text
}
