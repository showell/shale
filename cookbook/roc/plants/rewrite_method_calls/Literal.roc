## A call whose first argument is a constant numeric expression stays:
## `U64.to_str(5)` fixes the literal's type, and `(5).to_str()` leaves it
## to default (to `Dec`, "5.0"); so does `(2 + 3).to_str()`, and so does a
## name bound to one without an annotation, in a block (`n = 5`) or at the
## top (`top = 7`). Both forms check; only the expects tell them apart. A
## name annotated (`m : U64`) or a sum with a parameter in it (`n + 1`, n a
## U64) has its type, and the file's other calls are still turned. A loop
## variable over a list of literals or a range of them (`steps`, `k`), a
## `var` a literal starts (`$bad`, whatever it is reassigned) and a field
## of a record literal (`r.a`) default the same way, and stay; each typed
## by a suffix (`10.I64`, `0.I64`, `5.U64`) is turned.
Literal := [].{
	top = 7

	five : Str
	five = U64.to_str(5)

	sum : Str
	sum = U64.to_str(2 + 3)

	bound : Str
	bound = {
		n = 5
		U64.to_str(n)
	}

	annotated : Str
	annotated = {
		m : U64
		m = 5
		U64.to_str(m)
	}

	seven : Str
	seven = U64.to_str(top)

	next : U64 -> Str
	next = |n| U64.to_str(n + 1)

	shout : Str -> Str
	shout = |name| Str.concat(name, "!")

	looped : Str
	looped = {
		var $out = ""
		for steps in [10, 1000] {
			$out = $out.concat(I64.to_str(steps))
		}
		$out
	}

	typed_loop : Str
	typed_loop = {
		var $out = ""
		for steps in [10.I64, 1000] {
			$out = $out.concat(I64.to_str(steps))
		}
		$out
	}

	ranged : Str
	ranged = {
		var $out = ""
		for k in 0..<2 {
			$out = $out.concat(U64.to_str(k))
		}
		$out
	}

	counted : Str
	counted = {
		var $bad = 0
		for _s in ["a", "b"] {
			$bad = $bad + 1
		}
		I64.to_str($bad)
	}

	typed_counted : Str
	typed_counted = {
		var $bad = 0.I64
		for _s in ["a", "b"] {
			$bad = $bad + 1
		}
		I64.to_str($bad)
	}

	field : Str
	field = {
		r = { a: 5 }
		U64.to_str(r.a)
	}

	typed_field : Str
	typed_field = {
		r = { a: 5.U64 }
		U64.to_str(r.a)
	}
}

expect Literal.five == "5"
expect Literal.sum == "5"
expect Literal.bound == "5"
expect Literal.annotated == "5"
expect Literal.seven == "7"
expect Literal.next(4) == "5"
expect Literal.shout("a") == "a!"
expect Literal.looped == "101000"
expect Literal.typed_loop == "101000"
expect Literal.ranged == "01"
expect Literal.counted == "2"
expect Literal.typed_counted == "2"
expect Literal.field == "5"
expect Literal.typed_field == "5"
