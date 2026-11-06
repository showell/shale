## A module whose type has a shape, so it does declare a second `Lamp`.
Lamp :: { lit : Bool }.{
	on : Lamp
	on = { lit: True }
}
