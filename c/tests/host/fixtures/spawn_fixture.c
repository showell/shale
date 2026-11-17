// The program spawn's tests run, so they never depend on /bin/sh: the verbs
// of Zig Shale's src/shale_fixture.zig that spawn's tests use, and the attacks
// on the process boundary that the C's tests plant. Each verb is one
// behaviour a test needs, and nothing else:
//
//     lines N [TEXT]         N lines, "TEXT i" (TEXT "line")
//     exit CODE              exits CODE, printing nothing
//     env NAME               the variable NAME's value, or "(unset)"
//     cat                    stdin to stdout
//     sleep MS               sleeps, then prints "woke"
//     pwd                    the working directory
//     stderr TEXT            TEXT on stderr
//     kill-self              ends by SIGKILL, its own
//     ignore-term MS         TERM ignored, then sleeps
//     pid-sleep MS           "pid N", its own, then sleeps
//     spill N                N bytes of 'x' with no newline
//     spill-lines N          N lines of 99 'x's
//     burn MS                spins until it has used MS of CPU
//     burn-child MS          a child burns MS; this waits for it
//     grandchild-exit MS     "grandchild N", the pid of a child that sleeps
//                            MS holding the output; this exits 0 at once
//     grandchild-escape MS   the same, but the child leaves the group for a
//                            session of its own, out of `run`'s reach
//     grandchild-stay MS     the same grandchild, both ignoring TERM; this
//                            sleeps MS too
//     signal-parent SIG MS   SIG (15 or 1) to its parent, TERM ignored, then
//                            sleeps MS
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#include "slice.h"

static void sleep_ms(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0) {
    }
}

// Spins on the process's own CPU clock, so a busy box makes it take longer
// but never makes it burn less.
static void burn(long ms) {
    struct timespec ts;
    while (1) {
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
        if ((long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000 >= ms) return;
    }
}

static void ignore_term(void) {
    signal(SIGTERM, SIG_IGN);
}

// A grandchild that sleeps `ms` holding the output. This process says its
// pid, from the fork, so the line is there even when the group's TERM at
// this one's exit ends the grandchild before it would have run a line.
static void grandchild(long ms, bool deaf, bool escape) {
    // The child says it has left the group by closing its end, so this
    // exits only after: the group's TERM at this exit never finds it there.
    int ready[2];
    if (pipe(ready) != 0) _exit(3);
    pid_t pid = fork();
    if (pid == 0) {
        close(ready[0]);
        if (escape) setsid();
        close(ready[1]);
        if (deaf) ignore_term();
        sleep_ms(ms);
        _exit(0);
    }
    close(ready[1]);
    char byte;
    ssize_t r = read(ready[0], &byte, 1);
    (void)r;
    close(ready[0]);
    printf("grandchild %d\n", (int)pid);
    fflush(stdout);
}

static bool is(Str a, const char *b) { // cstr: a literal verb
    return str_eq(a, (Str){b, strlen(b)});
}

static long num(Str s) {
    return strtol(s.ptr, NULL, 10);
}

int main(int argc, char **argv) {
    SLICE_TYPE(Args, char *);
    Args args = {argv, (size_t)argc};
    if (args.len < 2) return 2;
    Str verb = {at(args, 1), strlen(at(args, 1))};
    Str arg = args.len > 2 ? (Str){at(args, 2), strlen(at(args, 2))} : S("");
    if (is(verb, "lines")) {
        const char *text = args.len > 3 ? at(args, 3) : "line";
        for (long i = 1; i <= num(arg); i++) printf("%s %ld\n", text, i);
    } else if (is(verb, "exit")) {
        return (int)num(arg);
    } else if (is(verb, "env")) {
        const char *v = getenv(arg.ptr);
        printf("%s\n", v != NULL ? v : "(unset)");
    } else if (is(verb, "cat")) {
        int c = 0;
        while ((c = getchar()) != EOF) putchar(c);
    } else if (is(verb, "sleep")) {
        sleep_ms(num(arg));
        printf("woke\n");
    } else if (is(verb, "pwd")) {
        char path[4096];
        if (getcwd(path, sizeof path) == NULL) return 3;
        printf("%s\n", path);
    } else if (is(verb, "stderr")) {
        fprintf(stderr, "%s\n", arg.ptr);
    } else if (is(verb, "kill-self")) {
        fflush(stdout);
        kill(getpid(), SIGKILL);
    } else if (is(verb, "ignore-term")) {
        ignore_term();
        printf("ignoring\n");
        fflush(stdout);
        sleep_ms(num(arg));
    } else if (is(verb, "pid-sleep")) {
        printf("pid %d\n", (int)getpid());
        fflush(stdout);
        sleep_ms(num(arg));
    } else if (is(verb, "spill")) {
        for (long i = 0; i < num(arg); i++) putchar('x');
    } else if (is(verb, "spill-lines")) {
        for (long i = 0; i < num(arg); i++) {
            for (int k = 0; k < 99; k++) putchar('x');
            putchar('\n');
        }
    } else if (is(verb, "burn")) {
        burn(num(arg));
    } else if (is(verb, "burn-child")) {
        pid_t pid = fork();
        if (pid == 0) {
            burn(num(arg));
            _exit(0);
        }
        int status = 0;
        waitpid(pid, &status, 0);
    } else if (is(verb, "grandchild-exit")) {
        grandchild(num(arg), false, false);
    } else if (is(verb, "grandchild-escape")) {
        grandchild(num(arg), false, true);
    } else if (is(verb, "grandchild-stay")) {
        ignore_term();
        grandchild(num(arg), true, false);
        sleep_ms(num(arg));
    } else if (is(verb, "signal-parent")) {
        ignore_term();
        printf("signalling\n");
        fflush(stdout);
        kill(getppid(), (int)num(arg));
        sleep_ms(args.len > 3 ? strtol(at(args, 3), NULL, 10) : 0);
    } else {
        return 2;
    }
    return 0;
}
