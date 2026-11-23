module [four, framed]

# An annotation whose type starts on the line after its colon, as `roc fmt`
# writes one that holds a stacked record (roc-apps' Skyline.silhouette).
four : # findings 1
	U8, U8, U8, U8 -> U8
four = |a, b, c, d| a + b + c + d

framed : # findings 1
	U8,
	U8,
	{
		left : U8,
		right : U8,
		top : U8,
	} -> U8
framed = |a, b, { left, right, top }| a + b + left + right + top
