## The mountains, a second user of Sun's shared member and Sky's top colour.
import Sky
import Sun

Mountains :: [].{

	## Reads two members others read too.
	height : Sun -> F64
	height = |sun| Sun.shared(sun) + Str.count_utf8_bytes(Sky.top_color).to_f64()
}
