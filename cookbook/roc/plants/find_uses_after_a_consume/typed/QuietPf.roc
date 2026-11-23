## The platform the typed plants' app names: a `Str`, handed to no host.
platform ""
	requires {
		main : Str
	}
	exposes []
	packages {}
	provides { "plant_main": main_for_host }

main_for_host : Str
main_for_host = main
