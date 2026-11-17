// keep.c: what `run` keeps of a program's output. Pure: bytes stream
// in, and each line is filtered, then kept from both ends. `not_re` removes
// the lines it matches from the stream; of the rest, `re` keeps those that
// match, and `before` and `after` keep context lines around a match as grep
// does. The first `head` lines and a ring of the last `tail` are kept, each
// cut at `width`.
//
// An arena cannot free a small allocation, so a dropped line's buffer is
// handed back to a slot and written over by a later line. Every line buffer
// belongs to exactly one of the head, the tail's ring, the context ring and
// the scratch slot a line is cut into, so the bytes held stay bounded by
// `(head + tail + before + 1)` slots of at most twice `width` plus the cut
// mark, and one line of at most `KEEP_LINE_MOST`, whatever the program
// prints.

#include "buf.h"

// The most of one line the filter reads. A longer line is matched on its
// first `KEEP_LINE_MOST` bytes and kept cut, as any long line is.
enum { KEEP_LINE_MOST = 64 * 1024 };

// The mark a cut line ends with, U+2026.
#define KEEP_CUT_MARK "\xe2\x80\xa6"

// A compiled pattern, as the host's regex gives one. `matches` says whether
// the line holds a match and adds the search's work to `*work` (what the
// host charges as steps). A pattern with `matches` NULL is none.
typedef struct {
    void *ctx;
    bool (*matches)(void *ctx, Str line, uint64_t *work);
} KeepPattern;

// What to keep of the output, its defaults those of `keep_options_default`,
// which fills one through a pointer (c/docs/style.md, "Declarations", has
// the rule on returning a struct by value).
typedef struct {
    // Keep only lines that match, plus context.
    KeepPattern re;
    // Remove lines that match from the stream: neither kept nor context.
    KeepPattern not_re;
    // Context lines kept before a match; only with `re`.
    size_t before;
    // Context lines kept after a match.
    size_t after;
    // Lines kept from the start.
    size_t head;
    // Lines kept from the end.
    size_t tail;
    // The bytes of a line kept; a longer line is cut and marked.
    size_t width;
} KeepOptions;

static void keep_options_default(KeepOptions *o) {
    memset(o, 0, sizeof *o);
    o->width = 200;
}

// What was kept, and the counts.
typedef struct {
    // The kept lines, with `… N lines left out …` where lines were dropped
    // between head and tail. In the keep's arena.
    Str out;
    // Every line of the output.
    uint64_t total;
    // Lines that passed the filter, as grep -c counts: context is not counted.
    uint64_t matched;
    // The lines in `out`.
    uint64_t kept;
    // The filter's work, what the host charges besides the bytes read.
    uint64_t work;
} Kept;

// One line's buffer: `len` bytes of a line in `cap` bytes of room.
typedef struct {
    char *ptr;
    size_t len;
    size_t cap;
} KeepSlot;

SLICE_TYPE(KeepSlots, KeepSlot);
VEC_DEFINE(KeepHead, KeepSlot);

// The streaming keep: `keep_feed` it bytes, then `keep_finish`.
typedef struct {
    Arena *arena;
    KeepOptions o;
    // The line being read, up to `KEEP_LINE_MOST` bytes of it.
    Buf line;
    // Whether the line being read ran past `KEEP_LINE_MOST`.
    bool line_over;
    // Where a line is cut before it is passed on; a buffer handed back by
    // the ring it displaced, or empty.
    KeepSlot scratch;
    // The first `head` lines passed.
    KeepHead head;
    // The last `tail` lines passed, a ring starting at `ring_at`.
    KeepSlots ring;
    size_t ring_len;
    size_t ring_at;
    // The lines before the next match, for `before`; a ring like `ring`.
    KeepSlots back;
    size_t back_len;
    size_t back_at;
    // How many lines after a match are still to be kept as context.
    size_t after_left;
    // The lines that passed the filter into the keep, dropped ones included.
    uint64_t passed;
    uint64_t total;
    uint64_t matched;
    uint64_t work;
} Keep;

// A keep over `a`; STOP when the arena refuses the rings (a `tail` or
// `before` too big for it), which the caller reads as the arena budget.
static STATUS_MUST_USE Status keep_init(Keep *k, Arena *a, KeepOptions o) {
    memset(k, 0, sizeof *k);
    k->arena = a;
    k->o = o;
    k->line = buf_make(a);
    k->head = KeepHead_make(a);
    size_t back_len = o.re.matches != NULL ? o.before : 0;
    KeepSlot *ring = ARENA_ARRAY(a, KeepSlot, o.tail);
    if (ring == NULL) return STOP;
    KeepSlot *back = ARENA_ARRAY(a, KeepSlot, back_len);
    if (back == NULL) return STOP;
    if (o.tail > 0) memset(ring, 0, o.tail * sizeof(KeepSlot));
    if (back_len > 0) memset(back, 0, back_len * sizeof(KeepSlot));
    k->ring.ptr = ring;
    k->ring.len = o.tail;
    k->back.ptr = back;
    k->back.len = back_len;
    return OK;
}

static bool keep_pattern_matches(KeepPattern p, Str line, uint64_t *work) {
    return p.matches(p.ctx, line, work);
}

// Room for `need` bytes in `s`, its bytes not kept. A slot grows to twice
// what it had, so a slot that is written over and over takes at most about
// twice its largest line in all.
static STATUS_MUST_USE Status keep_slot_room(Arena *a, KeepSlot *s, size_t need) {
    if (s->cap >= need) return OK;
    // DRAGON: use after free. A big buffer freed here goes back to the
    // system, so a second slot holding the same pointer would be written
    // through after its free.
    // REMEDY: every move between the head, the rings and the scratch swaps
    // two slots whole (keep_pass, keep_end_line_inner), or copies one into
    // the head and clears where it was, so no buffer is in two slots.
    size_t want = s->cap > SIZE_MAX / 2 ? SIZE_MAX : 2 * s->cap;
    if (want < need) want = need;
    char *p = arena_alloc(a, want, 1);
    if (p == NULL) return STOP;
    // A big buffer goes back to the system; a small one stays until the
    // arena does, and the doubling above bounds how many there are.
    if (s->cap > 0) arena_free(a, s->ptr, s->cap);
    s->ptr = p;
    s->cap = want;
    s->len = 0;
    return OK;
}

// `l` cut into the scratch slot: cut to `width` and marked if longer or
// `over`.
static STATUS_MUST_USE Status keep_cut(Keep *k, Str l, bool over) {
    bool whole = l.len <= k->o.width && !over;
    size_t n = l.len < k->o.width ? l.len : k->o.width;
    Str mark = S(KEEP_CUT_MARK);
    size_t need = whole ? l.len : n + mark.len;
    TRY(keep_slot_room(k->arena, &k->scratch, need));
    Bytes room = {k->scratch.ptr, k->scratch.cap};
    if (n > 0) memcpy(room.ptr, l.ptr, n);
    if (!whole) {
        Bytes tail = SLICE_SUB(Bytes, room, n, need);
        memcpy(tail.ptr, mark.ptr, mark.len);
    }
    k->scratch.len = need;
    return OK;
}

// Takes the line in `*from` (the scratch slot, or a context slot): into the
// head while it has room, else into the tail's ring, dropping the oldest.
// `*from` is left holding the buffer the line displaced, or none, so no
// buffer is ever in two places.
static STATUS_MUST_USE Status keep_pass(Keep *k, KeepSlot *from) {
    k->passed++;
    if (k->head.len < k->o.head) {
        TRY(KeepHead_push(&k->head, *from));
        memset(from, 0, sizeof *from);
        return OK;
    }
    if (k->ring.len == 0) {
        from->len = 0;
        return OK;
    }
    KeepSlot *to = NULL;
    if (k->ring_len < k->ring.len) {
        to = &at(k->ring, (k->ring_at + k->ring_len) % k->ring.len);
        k->ring_len++;
    } else {
        to = &at(k->ring, k->ring_at);
        k->ring_at = (k->ring_at + 1) % k->ring.len;
    }
    KeepSlot old = *to;
    *to = *from;
    *from = old;
    from->len = 0;
    return OK;
}

// Ends the line being read: counts it, filters it, and passes what the
// filter keeps.
static STATUS_MUST_USE Status keep_end_line_inner(Keep *k) {
    k->total++;
    Str l = buf_view(&k->line);
    if (k->o.not_re.matches != NULL && keep_pattern_matches(k->o.not_re, l, &k->work)) return OK;
    bool hit = k->o.re.matches == NULL || keep_pattern_matches(k->o.re, l, &k->work);
    if (hit) {
        k->matched++;
        // The context before the match, in order, then the match.
        while (k->back_len > 0) {
            KeepSlot *s = &at(k->back, k->back_at);
            k->back_at = (k->back_at + 1) % k->back.len;
            k->back_len--;
            TRY(keep_pass(k, s));
        }
        k->back_at = 0;
        TRY(keep_cut(k, l, k->line_over));
        TRY(keep_pass(k, &k->scratch));
        k->after_left = k->o.after;
    } else if (k->after_left > 0) {
        k->after_left--;
        TRY(keep_cut(k, l, k->line_over));
        TRY(keep_pass(k, &k->scratch));
    } else if (k->back.len > 0) {
        TRY(keep_cut(k, l, k->line_over));
        KeepSlot *to = NULL;
        if (k->back_len < k->back.len) {
            to = &at(k->back, (k->back_at + k->back_len) % k->back.len);
            k->back_len++;
        } else {
            to = &at(k->back, k->back_at);
            k->back_at = (k->back_at + 1) % k->back.len;
        }
        KeepSlot old = *to;
        *to = k->scratch;
        k->scratch = old;
        k->scratch.len = 0;
    }
    return OK;
}

// The line is cleared on every way out, a refusal's among them.
static STATUS_MUST_USE Status keep_end_line(Keep *k) {
    Status s = keep_end_line_inner(k);
    buf_clear(&k->line);
    k->line_over = false;
    return s;
}

// Takes the program's next bytes, any number.
// DRAGON: memory. The bytes are a program's, unbounded, and a line may
// never end, so a line held whole could grow with the output.
// REMEDY: the line being read is held to KEEP_LINE_MOST (`room`), the rest
// of it only marked as `line_over`, so one endless line holds 64 KiB, not
// the output. test_keep's flood and test_spawn's 2 MB line hold it.
static STATUS_MUST_USE Status keep_feed(Keep *k, Str bytes) {
    Str rest = bytes;
    while (rest.len > 0) {
        const char *nl = str_find_byte(rest, '\n');
        size_t piece_len = nl != NULL ? (size_t)(nl - rest.ptr) : rest.len;
        size_t room = KEEP_LINE_MOST - k->line.len;
        if (piece_len > room) k->line_over = true;
        TRY(buf_str(&k->line, str_sub(rest, 0, piece_len < room ? piece_len : room)));
        if (nl != NULL) {
            TRY(keep_end_line(k));
            rest = str_sub(rest, piece_len + 1, rest.len);
        } else {
            rest = str_sub(rest, rest.len, rest.len);
        }
    }
    return OK;
}

static STATUS_MUST_USE Status keep_line_out(Buf *out, KeepSlot s) {
    TRY(buf_str(out, (Str){s.ptr, s.len}));
    return buf_byte(out, '\n');
}

// Ends the output, taking a last line that had no newline, and fills `*kept`.
static STATUS_MUST_USE Status keep_finish(Keep *k, Kept *kept) {
    if (k->line.len > 0 || k->line_over) TRY(keep_end_line(k));
    Buf out = buf_make(k->arena);
    KeepHeadItems head = KeepHead_items(&k->head);
    for (size_t i = 0; i < head.len; i++) TRY(keep_line_out(&out, at(head, i)));
    uint64_t left_out = k->passed - head.len - k->ring_len;
    if (left_out > 0) {
        TRY(buf_lit(&out, KEEP_CUT_MARK " "));
        TRY(buf_u64(&out, left_out));
        TRY(left_out == 1 ? buf_lit(&out, " line left out " KEEP_CUT_MARK "\n") : buf_lit(&out, " lines left out " KEEP_CUT_MARK "\n"));
    }
    for (size_t i = 0; i < k->ring_len; i++) TRY(keep_line_out(&out, at(k->ring, (k->ring_at + i) % k->ring.len)));
    kept->out = buf_view(&out);
    kept->total = k->total;
    kept->matched = k->matched;
    kept->kept = head.len + k->ring_len;
    kept->work = k->work;
    return OK;
}
