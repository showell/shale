## A lamp whose members Room uses only in tests, and that Room uses beside
## its own nouns.
Lamp :: { watts : F64 }.{

	## Room uses it in an expect alone.
	glow : Lamp -> F64
	glow = |Lamp.({ watts })| watts

	## Room uses it in a value only an expect reads.
	bright : Lamp -> F64
	bright = |Lamp.({ watts })| watts * 2.0

	## Only the gates app uses it.
	dim : Lamp -> F64
	dim = |Lamp.({ watts })| watts / 2.0

	## Lamp's own `doubled` uses it, and Lamp is no other module.
	lit : Lamp -> Bool
	lit = |Lamp.({ watts })| watts > 0.0

	doubled : Lamp -> Bool
	doubled = |l| l.lit()

	## Nobody outside uses it.
	alone : Lamp -> F64
	alone = |Lamp.({ watts })| watts
}
