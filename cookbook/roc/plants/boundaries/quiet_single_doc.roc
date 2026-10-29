# Quiet for boundaries' `single`: a rule's one home, its reason written
# down in its doc.

## **AN EXCERPT IS 350 BYTES**: the rule's one home, which a caller asks.
excerpt : List(U8) -> List(U8)
excerpt = |s| List.take_first(s, 350)

preview : List(U8) -> List(U8)
preview = |s| {
	e = excerpt(s)
	List.concat(e, [46, 46, 46])
}
