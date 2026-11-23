## Quiet for `preposition_split`: one noun, one preposition, and a
## preposition first (Roc's conversions).
Text := [].{
	line_at : Str, U64 -> Str

	line_at_end : Str -> Str

	from_str : Str -> Text

	to_str : Text -> Str
}
