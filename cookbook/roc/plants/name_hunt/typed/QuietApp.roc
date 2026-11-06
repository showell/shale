## The root the typed plants are judged on: `roc-types` reads an app's
## modules.
app [main] { pf: platform "./QuietPf.roc" }

import PhraseNameInferred
import QuestionInferred
import QuietPhraseNameInferred
import QuietQuestionAlias
import QuietSingularListInferred
import SingularListInferred
import SingularListParameterInferred

main : Str
main = if SingularListInferred.triangle(1) == QuietSingularListInferred.triangles(1) or SingularListParameterInferred.size_of([1]) == 1 or QuestionInferred.has_room([1]) or QuietQuestionAlias.is_short("a") or PhraseNameInferred.in_view(1) == QuietPhraseNameInferred.from_made(QuietPhraseNameInferred.at_step(1)) "y" else "n"
