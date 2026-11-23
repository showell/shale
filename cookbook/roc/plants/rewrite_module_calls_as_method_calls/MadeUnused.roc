## A rewrite that leaves an import unused: `Ref` is named only by the call
## it turns, so the run reports `import Ref` as made unused, for the sweep
## to delete.
import Ref

MadeUnused := [].{
	shown = |r| Ref.to_str(r)
}
