module [Tilt, tilt_2, edges]

# The edges of vocabulary's word splitting, each line a case; tour, then tilt,
# then tour again on one comment line come out once each, sorted.
Tilt : [TiltLeft, TILTMode, Tilt2]

tilt_2 : U64
tilt_2 = 2

edges : U64 -> Str
edges = |tilt2| {
	tilt_tilt = tilt2 + tilt2 # the same name twice gives one finding
	x__tilt__y = tilt_tilt
	said = "a \"# tilt\" string" # journey after an escaped quote
	sign = '#' # tour after a character
	shown = "${Num.to_str(x__tilt__y)}" #tilt with no space
	# A word that only starts like one passes: tilted, tours, journeys.
	# Bytes past ASCII split words too: tiltéd and éjourney.
	## TOUR in capitals is no word of the list: TILT neither. A slant trip is,
	## in words_edges.txt.
	Str.concat(said, Str.concat(sign.to_str(), shown))
}
