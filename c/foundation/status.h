// status.h: what every function that can stop returns. `STOP` means the run
// stopped and its context (`RunCtx`, err.h) says where and why; a container
// that returns `STOP` was refused by its arena, which the caller reads as the
// arena budget (`run_oom`). err.h is the convention's home; this is its one
// enum, apart so that the containers below err.h can return it.

#ifndef SHALE_STATUS_H
#define SHALE_STATUS_H

typedef enum { OK = 0, STOP = 1 } Status;

// On every function that returns a Status. A STOP dropped on the floor lets
// the run go on past an arena refusal, where it must stop, so GCC refuses an
// answer left unused, even one cast to void. TCC lacks the check, so the GCC
// builds hold the C to it, and check_discipline's `must use` rule holds every
// such function to carry it.
#if defined(__GNUC__) && !defined(__TINYC__)
#define STATUS_MUST_USE __attribute__((warn_unused_result))
#else
#define STATUS_MUST_USE
#endif

// Returns STOP from the enclosing function when `expr` does.
#define TRY(expr)                           \
    do {                                    \
        if ((expr) != OK) return STOP;      \
    } while (0)

#endif
