app [main] {
	pf: platform "../platform/main.roc",
	shared: "../shared/main.roc",
}

import Ride

main = Ride.count
