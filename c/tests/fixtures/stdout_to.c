// stdout_to.c: runs a program with a stdout that refuses its writes, for the
// command block's check of a failed stdout write. `stdout_to WHAT PROG
// ARGS...`: WHAT is `full` (/dev/full, so a write is ENOSPC), `broken` (a
// pipe whose reading end is shut, EPIPE) or `closed` (no fd 1, EBADF). The
// program keeps this one's stderr, where it says the failure.
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "slice.h"

SLICE_TYPE(Args, char *);

int main(int argc, char **argv) {
    Args args = {argv, (size_t)argc};
    if (args.len < 3) {
        fprintf(stderr, "usage: stdout_to full|broken|closed PROG ARGS...\n");
        return 2;
    }
    const char *what = at(args, 1);
    if (strcmp(what, "full") == 0) {
        int fd = open("/dev/full", O_WRONLY);
        if (fd < 0 || dup2(fd, 1) != 1) return 3;
        close(fd);
    } else if (strcmp(what, "broken") == 0) {
        int ends[2] = {-1, -1};
        if (pipe(ends) != 0) return 3;
        SLICE_TYPE(Ends, int);
        Ends e = {ends, 2};
        if (dup2(at(e, 1), 1) != 1) return 3;
        close(at(e, 0));
        close(at(e, 1));
    } else if (strcmp(what, "closed") == 0) {
        close(1);
    } else {
        fprintf(stderr, "stdout_to: `%s` is not full, broken or closed\n", what);
        return 2;
    }
    execv(at(args, 2), &at(args, 2));
    fprintf(stderr, "stdout_to: %s could not be run\n", at(args, 2));
    return 127;
}
