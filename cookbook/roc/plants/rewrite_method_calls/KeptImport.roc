## The near miss of MadeUnused: the call turns, and the annotation still
## names `Ref`, so the import is still used and nothing is reported.
import Ref

KeptImport := [].{
	shown : Ref -> Str
	shown = |r| Ref.to_str(r)
}
