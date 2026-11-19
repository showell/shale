// shale_gate.c: the `shale-gate` program, the PreToolUse hook's classifier
// (c/gate/gate.c decides): it reads the hook's JSON on stdin, exits 0 to pass
// the call and 2 to refuse it, saying why on stderr. tools/shale-gate.sh runs
// it, and the C build makes it (c/build/gate.shale); the build does not
// install the hook.
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#include "gate/gate.c"

enum {
    // The arena's limit: far past what a megabyte of JSON holds decoded, so
    // the gate never refuses input that Zig's would read.
    GATE_ARENA_LIMIT = 1 << 30,
};

// Stdin into `b`, at most GATE_INPUT_MAX bytes; false when it cannot be read
// or holds more, as Zig's `allocRemaining` refuses it.
static bool gate_read_stdin(Buf *b) {
    while (true) {
        if (buf_reserve(b, 64 * 1024) != OK) return false;
        Bytes room = {b->ptr, b->cap};
        ssize_t n = read(0, &at(room, b->len), b->cap - b->len);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return false;
        if (n == 0) return true;
        b->len += (size_t)n;
        if (b->len > GATE_INPUT_MAX) return false;
    }
}

// The folder this program's own file is in, every link followed (the
// kernel's /proc/self/exe); empty when it cannot be read, so
// no launch by path passes (`gate_ours`).
static Str gate_beside(Arena *a) {
    char *buf = arena_alloc(a, PATH_MAX, 1);
    if (buf == NULL) return S("");
    ssize_t n = readlink("/proc/self/exe", buf, PATH_MAX - 1);
    if (n <= 0) return S("");
    Str self = {buf, (size_t)n};
    size_t cut = self.len;
    while (cut > 0 && str_byte(self, cut - 1) != '/') cut--;
    if (cut <= 1) return S("");
    return str_sub(self, 0, cut - 1);
}

int main(void) {
    // The gate runs no script, so its arena's refusal is a local of its own.
    bool refused = false;
    Arena a;
    arena_init(&a, GATE_ARENA_LIMIT, &refused);
    Buf input = buf_make(&a);
    bool read_ok = gate_read_stdin(&input);
    Str home = {"", 0};
    bool has_home = gate_home(&home);
    int code = gate_run(&a, read_ok, buf_view(&input), has_home, home, gate_beside(&a));
    arena_deinit(&a);
    return code;
}
