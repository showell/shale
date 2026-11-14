## The root the typed plants are judged on: `roc-types` reads an app's
## modules.
app [main] { pf: platform "./QuietPf.roc" }

import PhraseNameFromInferred
import PhraseNameInferred
import QuestionInferred
import QuietPhraseNameFromInferred
import QuietPhraseNameInferred
import QuietPhraseNameTypelessInferred
import QuietQuestionAlias
import QuietSingularListInferred
import SingularListInferred
import SingularListParameterInferred

main : Str
main = if SingularListInferred.triangle(1) == QuietSingularListInferred.triangles(1) or SingularListParameterInferred.size_of([1]) == 1 or QuestionInferred.has_room([1]) or QuietQuestionAlias.is_short("a") or PhraseNameInferred.in_view(1) == QuietPhraseNameInferred.from_made(QuietPhraseNameInferred.at_step(1)) or PhraseNameFromInferred.from_clock(1) == QuietPhraseNameTypelessInferred.from_digits(1) or QuietPhraseNameFromInferred.from_held(1).count == 1 "y" else "n"
