## Whether a value's type comes from unsuffixed number literals alone, read
## from the file's tree (number_literals.shale). Such a value's type
## defaults (to `Dec`) when it is a method call's receiver, so
## `U64.to_str(5)` is "5" while `(5).to_str()` is "5.0", and such a call
## stays as it is.
import Source

Literals := [].{

	## Whether node `place` is a constant numeric expression: untyped number
	## literals (`5`, `-3`, `0x1F`, `1.5`, `'a'`; not `5.U64`) joined only by
	## operators and parentheses (a one-element tuple), as in `5`, `(5)`,
	## `2 + 3` or `-(1 + 2)`.
	is_constant_numeric : Source.File, U64 -> Bool
	is_constant_numeric = |file, place| {
		node = Source.node_at(file, place)
		if untyped_number_literals.contains(node.kind) {
			Bool.True
		} else {
			joins = node.kind == "e-binop" or node.kind == "unary" or (node.kind == "e-tuple" and node.kids.len() == 1)
			joins and !(node.kids.is_empty()) and node.kids.all(|kid| Literals.is_constant_numeric(file, kid))
		}
	}

	## The name a definition `name = <value>` binds, or `""` for any other
	## node.
	defined_name : Source.File, Source.Node -> Str
	defined_name = |file, node|
		if node.kind == "s-decl" and node.kids.len() == 2 {
			pattern = Source.kid_node(file, node, 1)
			if pattern.kind == "p-ident" {
				pattern.raw
			} else {
				""
			}
		} else {
			""
		}

	## Whether the value at node `place` takes its type from unsuffixed
	## number literals alone, so that as a receiver it defaults: a constant
	## numeric expression; a plain name bound, unannotated, to one, or to
	## another such name; a loop variable over a list of them or a range of
	## literals (`for n in [10, 1000]`, `for k in 0..<3`); a `var` whose start
	## is one; or a field of a record literal that holds one (`r.a` after
	## `r = { a: 5 }`).
	takes_literal_type : Source.File, U64 -> Bool
	takes_literal_type = |file, place| takes(file, place, Number, 0)
}

## The number literals whose type only their use fixes.
untyped_number_literals : Set(Str)
untyped_number_literals = Set.from_list(["e-int", "e-frac", "e-single-quote"])

## What a value is asked to be: the number itself, or what a `for` walks (a
## list literal of such numbers, a range of literals, or a name bound to
## either).
Asked : [Number, Items]

## How a plain name gets its type: from a definition's value, from a `for`
## that walks a value, or some other way (a parameter, an annotation, no
## binding found).
Binding : [Value(U64), Loop(U64), Unbound]

is_plain_name : Source.Node -> Bool
is_plain_name = |node| node.kind == "e-ident" and !(node.raw.contains("."))

has_parameter_named : Source.File, Source.Node, Str -> Bool
has_parameter_named = |file, lambda, name|
	lambda.kids.any(
		|kid| {
			args = Source.node_at(file, kid)
			args.kind == "args" and args.kids.any(|arg| is_parameter_named(file, arg, name))
		},
	)

is_parameter_named : Source.File, U64, Str -> Bool
is_parameter_named = |file, place, name| {
	pattern = Source.node_at(file, place)
	pattern.kind == "p-ident" and pattern.raw == name
}

## The binding of the plain name `name`, used at node `at`, that gives it
## its type. The binding that counts is the nearest one up the tree: in a
## block, the last definition before the statement holding the use; at the
## top level, any definition. A `var`'s type is its start's, so for a
## `$name` only its `var` counts, never a reassignment.
binding_of : Source.File, U64, Str -> Binding
binding_of = |file, at, name| {
	is_var = name.starts_with("$")
	var $child = at
	var $up = Source.node_at(file, at).parent
	while $up > 0 {
		scope = Source.node_at(file, $up)
		if scope.kind == "e-lambda" and has_parameter_named(file, scope, name) {
			return Unbound
		}
		if scope.kind == "s-for" and scope.kids.len() == 3 and $child == Source.kid(scope, 3) {
			if is_parameter_named(file, Source.kid(scope, 1), name) {
				return Loop(Source.kid(scope, 2))
			}
		}
		in_block = scope.kind == "statements" and scope.parent > 0 and Source.node_at(file, scope.parent).kind == "e-block"
		var $definition = 0
		var $annotated = Bool.False
		var $passed = Bool.False
		for kid in scope.kids {
			if in_block and kid == $child {
				$passed = Bool.True
			}
			if !($passed) {
				statement = Source.node_at(file, kid)
				if is_var {
					if statement.kind == "s-var" and statement.name == name and statement.kids.len() == 1 {
						$definition = kid
					}
				} else if Literals.defined_name(file, statement) == name {
					$definition = kid
				}
				if statement.kind == "s-type-anno" and statement.name == name {
					$annotated = Bool.True
				}
			}
		}
		if $definition > 0 {
			if $annotated {
				return Unbound
			}
			definition = Source.node_at(file, $definition)
			return Value(definition.kids.last() ?? 0)
		}
		$child = $up
		$up = scope.parent
	}
	Unbound
}

## `Literals.takes_literal_type`, for a number or for what a `for` walks, at
## most 8 bindings deep. One function for both, since each asks the other.
takes : Source.File, U64, Asked, U64 -> Bool
takes = |file, place, asked, depth|
	if depth > 8 {
		Bool.False
	} else if Literals.is_constant_numeric(file, place) {
		Bool.True
	} else {
		node = Source.node_at(file, place)
		if is_plain_name(node) {
			match binding_of(file, place, node.raw) {
				Unbound => Bool.False
				Loop(walked) => asked == Number and takes(file, walked, Items, depth + 1)
				Value(value) => takes(file, value, asked, depth + 1)
			}
		} else {
			match asked {
				Items =>
					node.kind == "e-list" and !(node.kids.is_empty()) and node.kids.all(|kid| takes(file, kid, Number, depth + 1))
				Number => field_takes(file, node, depth)
			}
		}
	}

## Whether a field access `r.a` reads a record literal's field that takes
## its type from number literals.
field_takes : Source.File, Source.Node, U64 -> Bool
field_takes = |file, node, depth|
	if node.kind == "e-field-access" and node.kids.len() == 2 {
		receiver = Source.kid_node(file, node, 1)
		segment = Source.kid_node(file, node, 2)
		if receiver.kind != "receiver" or receiver.kids.len() != 1 or segment.kind != "segment" {
			Bool.False
		} else {
			value_place = Source.kid(receiver, 1)
			value = Source.node_at(file, value_place)
			if !(is_plain_name(value)) {
				Bool.False
			} else {
				match binding_of(file, value_place, value.raw) {
					Value(bound) => {
						record = Source.node_at(file, bound)
						if record.kind != "e-record" {
							Bool.False
						} else {
							matching = record.kids.find_first(|kid| is_field_named(file, kid, segment.field))
							match matching {
								Ok(field_place) => takes(file, Source.kid(Source.node_at(file, field_place), 1), Number, depth + 1)
								Err(_) => Bool.False
							}
						}
					}
					_ => Bool.False
				}
			}
		}
	} else {
		Bool.False
	}

is_field_named : Source.File, U64, Str -> Bool
is_field_named = |file, place, name| {
	field = Source.node_at(file, place)
	field.kind == "field" and field.field == name and field.kids.len() == 1
}
