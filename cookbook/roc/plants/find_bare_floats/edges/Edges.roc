## The rule's edges: a type with arguments, a `where` clause, an effect's
## name, a local named like a helper, and a helper's helper.
Edges :: [].{

	Box(a) : { value : a, scale : F64 }

	Signal : [Quiet, Loud({ level : F64, label : Str })]

	measure : a -> F64 where [a.size : a -> F64]
	measure = |_x| 1.5

	run! : F64 => {}
	run! = |_x| {}

	shadowed : U8 -> U8
	shadowed = |n| {
		shared = 2
		_ = shared
		n
	}
}

shared : F64
shared = 1.5

leaf : F64
leaf = 1.5

branch : U8 -> F64
branch = |_n| leaf

expect shared == 1.5
expect branch(1) == 1.5
