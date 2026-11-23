## The root the typed plants are judged on: `roc-types` reads an app's
## modules.
app [main] { pf: platform "./QuietPf.roc" }

import BoolParam
import QuietBoolParamOther
import QuietBoolParamSole
import QuietBoolParamVars

main : Str
main = {
	empty = Str.is_empty("")
	full = Str.is_empty("x")
	"${BoolParam.label("a", Bool.True)}${BoolParam.label("b", Bool.False)}${QuietBoolParamVars.label("c", empty)}${QuietBoolParamVars.label("d", full)}${QuietBoolParamOther.label("e", Loud)}${QuietBoolParamOther.label("f", Quiet)}${QuietBoolParamOther.one(Bool.True).len().to_str()}${QuietBoolParamSole.word(Bool.True)}"
}
