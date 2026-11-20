## A module of escape.roc's own, beside it: a stop in it names this file's
## line, not the root's.
import pf.Shale

Reach := [].{
	## A read out of the root, from a module.
	climb! : {} => {}
	climb! = |{}| {
		_ = Shale.read!("../outside.txt")
		Shale.print!("read past the root from a module: the policy did NOT hold")
	}
}
