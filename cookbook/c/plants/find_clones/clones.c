// clones.c: a plant of find_clones' self-test, twins, a shared run and their
// near misses.
#include "test.h"

// A twin of sum_prices: the same shape, every name and number changed.
static int sum_weights(const Items *items, int floor) {
    int total = 0;
    for (size_t i = 0; i < items->len; i++) {
        Item it = at(items->ptr, i);
        if (it.weight < floor) continue;
        total += it.weight * 2;
        log_line(S("weight"), it.weight);
    }
    if (total > 100) total = 100;
    return total;
}

static int sum_prices(const Goods *goods, int least) {
    int sum = 0;
    for (size_t k = 0; k < goods->len; k++) {
        Good g = at(goods->ptr, k);
        if (g.price < least) continue;
        sum += g.price * 3;
        log_line(S("price"), g.price);
    }
    if (sum > 500) sum = 500;
    return sum;
}

// As long as the twins, and over the same items, but of another shape.
static int count_matches(const Items *items, Str want) {
    int found = 0;
    size_t i = 0;
    while (i < items->len) {
        if (str_eq(at(items->ptr, i).name, want)) found++;
        i++;
    }
    switch (found) {
    case 0:
        return -1;
    default:
        return found;
    }
}

// Six lines alike with write_trailer, the rest unlike.
static Status write_header(Buf *b, Str name) {
    if (name.len == 0) return STOP;
    TRY(buf_str(b, S("name: ")));
    TRY(buf_str(b, name));
    TRY(buf_str(b, S("\n")));
    TRY(buf_str(b, S("version: 1\n")));
    TRY(buf_str(b, S("kind: shale\n")));
    TRY(buf_str(b, S("end\n")));
    return OK;
}

static Status write_trailer(Buf *b, int code, Str name) {
    Arena *a = buf_arena(b);
    if (code < 0) {
        arena_note(a, S("negative"));
        return STOP;
    }
    TRY(buf_str(b, S("name: ")));
    TRY(buf_str(b, name));
    TRY(buf_str(b, S("\n")));
    TRY(buf_str(b, S("version: 1\n")));
    TRY(buf_str(b, S("kind: shale\n")));
    TRY(buf_str(b, S("end\n")));
    switch (code) {
    case 1:
        arena_note(a, S("one"));
        break;
    default:
        arena_note(a, S("many"));
    }
    return OK;
}

// Four lines alike with write_header, short of a run, then a string that
// differs: `S("kind: zig\n")` is not `S("kind: shale\n")`.
static Status write_banner(Buf *b, Str name) {
    while (name.len > 80) name = str_sub(name, 0, 80);
    TRY(buf_str(b, S("name: ")));
    TRY(buf_str(b, name));
    TRY(buf_str(b, S("\n")));
    TRY(buf_str(b, S("version: 1\n")));
    TRY(buf_str(b, S("kind: zig\n")));
    for (size_t k = 0; k < name.len; k++) buf_byte(b, at(name, k));
    return OK;
}
