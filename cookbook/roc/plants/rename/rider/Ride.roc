module [Rider, RiderState, RiderCamera, start]

Rider : { speed : U64 }

RiderState : [Riding(Rider), Resting]

RiderCamera : { follow : Rider }

start : U64 -> Rider
start = |speed| { speed }
