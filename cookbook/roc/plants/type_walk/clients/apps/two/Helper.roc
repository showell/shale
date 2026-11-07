## A module with no header of its own, which reads `geo` from its folder's
## app.
import geo.Dot

Helper :: [].{

	## A dot scaled out.
	far : Dot -> Dot
	far = |d| Dot.scale(d)
}
