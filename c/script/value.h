// value.h: a script's values: nil, booleans, 64-bit integers, byte strings, and
// tables, each a list or a map once it has an entry. A string is never changed
// once made. A table is shared by reference, so it has no identity beyond its
// pointer. Everything a value points at lives in the run's values arena, but a
// string literal, which points into the source.
//
// A Value is 24 bytes, past the 16 a function may return (c/docs/style.md),
// so every function that gives one fills an out-pointer.

#ifndef SHALE_VALUE_H
#define SHALE_VALUE_H

#include "../foundation/map.h"

typedef enum {
    VALUE_NIL,
    VALUE_BOOL,
    VALUE_INT,
    VALUE_STRING,
    VALUE_TABLE,
} ValueTag;

typedef struct Table Table;

typedef struct {
    ValueTag tag;
    union {
        bool boolean;
        int64_t integer;
        Str string;
        Table *table;
    } as;
} Value;

VEC_DEFINE(ValueVec, Value)
MAP_DEFINE(ValueMap, Value)
SLICE_TYPE(Values, Value);

// `{}` takes its kind from its first entry, and keeps it.
typedef enum {
    TABLE_EMPTY,
    TABLE_LIST,
    TABLE_MAP,
} TableKind;

typedef struct Table {
    TableKind kind;
    // A list's items, index 1 first. Only `pop` shrinks one.
    ValueVec list;
    // A map's entries, its keys in the order they were first set.
    ValueMap map;
} Table;

static inline void value_nil(Value *v) {
    v->tag = VALUE_NIL;
    v->as.integer = 0;
}

static inline void value_bool(Value *v, bool b) {
    v->tag = VALUE_BOOL;
    v->as.boolean = b;
}

static inline void value_int(Value *v, int64_t i) {
    v->tag = VALUE_INT;
    v->as.integer = i;
}

static inline void value_string(Value *v, Str s) {
    v->tag = VALUE_STRING;
    v->as.string = s;
}

static inline void value_table(Value *v, Table *t) {
    v->tag = VALUE_TABLE;
    v->as.table = t;
}

// Lua's truth: only nil and false are false.
static inline bool value_truthy(const Value *v) {
    if (v->tag == VALUE_NIL) return false;
    if (v->tag == VALUE_BOOL) return v->as.boolean;
    return true;
}

// The value's type as a message names it.
static inline Str value_type_name(const Value *v) {
    switch (v->tag) {
    case VALUE_NIL: return S("nil");
    case VALUE_BOOL: return S("a boolean");
    case VALUE_INT: return S("an integer");
    case VALUE_STRING: return S("a string");
    case VALUE_TABLE: break;
    }
    if (v->tag != VALUE_TABLE) c_bug("value: a value of no tag");
    if (v->as.table->kind == TABLE_LIST) return S("a list");
    if (v->as.table->kind == TABLE_MAP) return S("a map");
    if (v->as.table->kind != TABLE_EMPTY) c_bug("value: a table of no kind");
    return S("an empty table");
}

// A list's or a map's count of entries: what `count(t)` gives.
static inline size_t table_count(const Table *t) {
    return t->kind == TABLE_MAP ? ValueMap_len(&t->map) : t->list.len;
}

#endif
