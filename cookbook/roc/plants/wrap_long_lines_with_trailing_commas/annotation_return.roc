module [Checks]

# A line too wide whose one group to stack is its return type's: `roc fmt`
# then puts each argument on a line of its own. Joined, the group would put
# the annotation back on the line too wide, so it stays stacked (the rewrite
# once took its comma out and gave it back, pass after pass).
Checks := [].{
	constructor_checked : Ast.FunctionDefinition, Parser.ClassCode, Ast.Where -> Try({}, Lexer.Fault)
	constructor_checked = |_definition, _made, _site| Ok({})
}
