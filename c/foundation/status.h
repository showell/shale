// status.h: what every function that can stop returns. `STOP` means the run
// stopped and its context (`RunCtx`, err.h) says where and why; a container
// that returns `STOP` was refused by its arena, which the caller reads as the
// arena budget (`run_oom`). err.h is the convention's home; this is its one
// enum, apart so that the containers below err.h can return it.

#ifndef SHALE_STATUS_H
#define SHALE_STATUS_H

// DRAGON: every budget. A STOP dropped on the floor (`(void)buf_str(...)`, or a call whose answer is never tested) lets the run go on past an arena refusal with its data cut short, where Zig stops. The compiler doesn't hold a caller to TRY it: GCC warns of an unused answer only under warn_unused_result, which nothing here sets.
typedef enum { OK = 0, STOP = 1 } Status;

// Returns STOP from the enclosing function when `expr` does.
#define TRY(expr)                           \
    do {                                    \
        if ((expr) != OK) return STOP;      \
    } while (0)

#endif
