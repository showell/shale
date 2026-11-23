## The identifiers a node of Roc's tree holds, by its kind: the attribute
## that holds the name, or for a name a header or an import exposes in lower
## case, the `value` of a `text` kid.
import lib.Tree

Identifiers := [].{

	## The identifiers a node of the tree holds, in the order the Shale
	## original read them: each named attribute that is not empty, then each
	## `text` kid's value.
	of_node : List(Tree.Node), Tree.Node -> List(Str)
	of_node = |syntax_nodes, node| {
		named = name_attributes(node.kind)
			.map(|attribute_name| Tree.attribute(node, attribute_name))
			.keep_if(|name| name != "")
		if node.kind == "exposed-lower-ident" {
			named.concat(text_kid_values(syntax_nodes, node))
		} else {
			named
		}
	}
}

## Which attributes hold the identifier in each kind of node that has one. A
## `field` node may hold a field's name under either attribute. A `method` is
## a `where` clause's constraint, and a `p-as` the name an `as` pattern gives.
name_attributes : Str -> List(Str)
name_attributes = |kind|
	match kind {
		"ty" => ["name"]
		"ty-var" => ["raw"]
		"e-ident" => ["raw"]
		"p-ident" => ["raw"]
		"e-tag" => ["raw"]
		"p-tag" => ["raw"]
		"s-type-anno" => ["name"]
		"s-import" => ["raw"]
		"s-var" => ["name"]
		"anno-record-field" => ["name"]
		"record-field" => ["name"]
		"header" => ["name"]
		"field" => ["field", "name"]
		"segment" => ["field"]
		"e-method-call" => ["method"]
		"p-list-rest" => ["name"]
		"method" => ["name"]
		"p-as" => ["name"]
		"exposed-upper-ident" => ["text"]
		_ => []
	}

## The values of the node's `text` kids, those that are not empty.
text_kid_values : List(Tree.Node), Tree.Node -> List(Str)
text_kid_values = |syntax_nodes, node|
	node.kids
		.map(|kid| Tree.node_at(syntax_nodes, kid))
		.keep_if(|kid| kid.kind == "text")
		.map(|kid| Tree.attribute(kid, "value"))
		.keep_if(|value| value != "")
