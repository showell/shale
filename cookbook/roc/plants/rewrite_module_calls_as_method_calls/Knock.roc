## A refused call whose error makes trouble on another line (roc-chat's
## `Topics.stream!`): `"last".to_utf8().read!()` asks a list for Header's
## method, and the compiler, taking the refused value as known, also warns
## on the `if` (`unconditional condition`), a line no call is on. That
## warning is the refused call's, not a stray: the line's two calls stay,
## and the file's other call is still turned.
import Header

Knock := [].{
	later! : U64 => U64
	later! = |n| {
		values = Header.read!(Str.to_utf8("last"))
		after =
			match values {
				[only] => only.count_utf8_bytes()
				_ => 0
			}
		if (after > 0) after else n
	}

	size : List(U8) -> U64
	size = |bytes| List.len(bytes)
}

expect Knock.size([1]) == 1
