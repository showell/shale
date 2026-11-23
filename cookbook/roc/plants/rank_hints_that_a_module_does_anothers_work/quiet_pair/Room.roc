## A room with a lampshade, a word that starts with the lamp's name and is
## still no borrowed noun.
import Lamp

Room :: [].{

	## A lampshade is no lamp.
	lampshade_size : F64
	lampshade_size = 1.0

	## Room's own noun.
	room_size : F64
	room_size = 2.0

	## A whole-word lamp, but alone: no underscore follows.
	lamp : F64
	lamp = 3.0
}

sample_brightness : F64
sample_brightness = Lamp.bright(Lamp.({ watts: 1.0 }))

expect sample_brightness == 2.0

expect Lamp.glow(Lamp.({ watts: 1.0 })) == 1.0
