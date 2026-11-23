# Quiet for `short_name`: a loop's variable, a coordinate, a colour's
# channels and a triangle's corners may be one letter.
total = |start, cells| {
	var $i = start
	while $i < 3 {
		$i = $i + 1
	}
	for n in cells {
		dbg n
	}
	{ x, y, w } = { x: 1, y: 2, w: 3 }
	{ r, g, b, a } = { r: 1, g: 2, b: 3, a: 4 }
	spread = cells.map(|e| e * 2)
	doubled = List.map(cells, |e| e * 2)
	summed = cells.keep_if(|v| v > 1).walk(0, |sum, s| sum + s)
	$i + corners({ a: 1, b: 2, c: 3 }) + x + y + w + r + g + b + a + spread.len() + doubled.len() + summed
}

corners = |{ a, b, c }| a + b + c
