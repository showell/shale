## The root the typed plants are judged on: `roc-types` reads an app's
## modules.
app [main] { pf: platform "./QuietPf.roc" }

import QuestionInferred
import QuietQuestionAlias

main : Str
main = if QuestionInferred.has_room([1]) or QuietQuestionAlias.is_short("a") "y" else "n"
