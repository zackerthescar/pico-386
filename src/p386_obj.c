/*
 * pico386 VM heap objects: strings, tables, intern table.
 *
 * Pure C; no asm, no DOS calls. Designed to be host-testable.
 * Allocation: malloc, never free (see TODO_GC.md).
 */

#include <stdlib.h>
#include <string.h>
#include "p386_obj.h"

/* ---------- intern table -------------------------------------------- */

#define INTERN_INIT_CAP 64

typedef struct InternBucket {
    P386String *s;
} InternBucket;

static InternBucket *g_intern = 0;
static uint32_t      g_intern_cap = 0;
static uint32_t      g_intern_n = 0;

static uint32_t fnv1a(const char *p, uint32_t n) {
    uint32_t h = 2166136261u;
    uint32_t i;
    for (i = 0; i < n; i++) {
        h ^= (unsigned char)p[i];
        h *= 16777619u;
    }
    /* avoid hash 0 so we can use it later as a sentinel if useful */
    return h ? h : 1u;
}

static int intern_grow(void) {
    uint32_t new_cap = g_intern_cap ? g_intern_cap * 2 : INTERN_INIT_CAP;
    InternBucket *nb = (InternBucket *)calloc(new_cap, sizeof(InternBucket));
    uint32_t i;
    if (!nb) return 0;
    for (i = 0; i < g_intern_cap; i++) {
        P386String *s = g_intern[i].s;
        if (s) {
            uint32_t mask = new_cap - 1;
            uint32_t j = s->hash & mask;
            while (nb[j].s) j = (j + 1) & mask;
            nb[j].s = s;
        }
    }
    free(g_intern);
    g_intern = nb;
    g_intern_cap = new_cap;
    return 1;
}

static P386String *string_alloc(const char *data, uint32_t len, uint32_t hash) {
    P386String *s = (P386String *)malloc(sizeof(P386String) + len);
    if (!s) return 0;
    s->len = len;
    s->hash = hash;
    if (len) memcpy(s->data, data, len);
    s->data[len] = 0;
    return s;
}

P386String *p386_string_new(const char *data, uint32_t len) {
    return string_alloc(data, len, fnv1a(data, len));
}

P386String *p386_string_intern(const char *data, uint32_t len) {
    uint32_t hash, mask, i;
    P386String *s;

    if (g_intern_cap == 0 || g_intern_n * 2 >= g_intern_cap) {
        if (!intern_grow()) return p386_string_new(data, len);
    }
    hash = fnv1a(data, len);
    mask = g_intern_cap - 1;
    i = hash & mask;
    while (g_intern[i].s) {
        P386String *e = g_intern[i].s;
        if (e->hash == hash && e->len == len &&
            (len == 0 || memcmp(e->data, data, len) == 0)) {
            return e;
        }
        i = (i + 1) & mask;
    }
    s = string_alloc(data, len, hash);
    if (!s) return 0;
    g_intern[i].s = s;
    g_intern_n++;
    return s;
}

int p386_string_eq(const P386String *a, const P386String *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->hash != b->hash || a->len != b->len) return 0;
    return a->len == 0 || memcmp(a->data, b->data, a->len) == 0;
}

/* ---------- num -> string ------------------------------------------- */

/* 16.16 fixed-point -> decimal. Format "-?int(.frac)?".
 * Fractional digits: up to 4 (good enough for PICO-8's 16.16 resolution
 * of ~1.5e-5; trailing zeros stripped). */
P386String *p386_num_to_string(int32_t fp) {
    char buf[32];
    int  n = 0;
    uint32_t whole, frac;
    int neg = 0;
    if (fp < 0) { neg = 1; fp = -fp; }
    whole = ((uint32_t)fp) >> 16;
    frac  = ((uint32_t)fp) & 0xffffu;

    /* whole part */
    {
        char tmp[16];
        int  m = 0;
        if (whole == 0) tmp[m++] = '0';
        while (whole) { tmp[m++] = (char)('0' + (whole % 10)); whole /= 10; }
        if (neg) buf[n++] = '-';
        while (m > 0) buf[n++] = tmp[--m];
    }

    if (frac) {
        /* up to 4 decimal digits */
        uint32_t f = frac * 10000u; /* /65536 done by shift below */
        uint32_t v = f >> 16;       /* 0..9999 */
        char d[4];
        int i;
        d[0] = (char)('0' + (v / 1000) % 10);
        d[1] = (char)('0' + (v / 100)  % 10);
        d[2] = (char)('0' + (v / 10)   % 10);
        d[3] = (char)('0' + v % 10);
        /* strip trailing zeros */
        i = 4;
        while (i > 1 && d[i-1] == '0') i--;
        buf[n++] = '.';
        { int k; for (k = 0; k < i; k++) buf[n++] = d[k]; }
    }
    return p386_string_intern(buf, (uint32_t)n);
}

/* ---------- concat -------------------------------------------------- */

P386String *p386_value_concat(const P386Value *a, const P386Value *b) {
    P386String *sa = 0;
    P386String *sb = 0;
    P386String *tmp_a = 0;
    P386String *tmp_b = 0;
    uint32_t la, lb;
    char *buf;
    P386String *out;

    if (a->tag == P386_TAG_STR)      sa = (P386String *)(uintptr_t)a->value;
    else if (a->tag == P386_TAG_NUM) sa = tmp_a = p386_num_to_string(a->value);
    else return 0;
    if (b->tag == P386_TAG_STR)      sb = (P386String *)(uintptr_t)b->value;
    else if (b->tag == P386_TAG_NUM) sb = tmp_b = p386_num_to_string(b->value);
    else return 0;
    if (!sa || !sb) return 0;

    la = sa->len;
    lb = sb->len;
    /* fast path: empty operand */
    if (la == 0) return sb;
    if (lb == 0) return sa;

    buf = (char *)malloc((size_t)la + lb);
    if (!buf) return 0;
    memcpy(buf, sa->data, la);
    memcpy(buf + la, sb->data, lb);
    out = p386_string_intern(buf, la + lb);
    free(buf);
    (void)tmp_a; (void)tmp_b;
    return out;
}

/* ---------- table --------------------------------------------------- */

#define TABLE_INIT_CAP 4

static int value_eq(const P386Value *a, const P386Value *b) {
    if (a->tag != b->tag) return 0;
    if (a->tag == P386_TAG_STR) {
        /* with interning, pointer compare is sufficient, but be safe. */
        return p386_string_eq((const P386String *)(uintptr_t)a->value,
                              (const P386String *)(uintptr_t)b->value);
    }
    return a->value == b->value;
}

P386Table *p386_table_new(uint32_t array_hint, uint32_t hash_hint) {
    P386Table *t = (P386Table *)malloc(sizeof(P386Table));
    uint32_t cap;
    (void)hash_hint;
    if (!t) return 0;
    cap = array_hint ? array_hint : TABLE_INIT_CAP;
    if (cap < TABLE_INIT_CAP) cap = TABLE_INIT_CAP;
    t->entries = (P386TableEntry *)calloc(cap, sizeof(P386TableEntry));
    if (!t->entries) { free(t); return 0; }
    t->cap = cap;
    t->len = 0;
    t->array_len = 0;
    return t;
}

static int table_grow(P386Table *t) {
    uint32_t new_cap = t->cap * 2;
    P386TableEntry *ne = (P386TableEntry *)realloc(t->entries,
                                                   new_cap * sizeof(P386TableEntry));
    if (!ne) return 0;
    memset(ne + t->cap, 0, (new_cap - t->cap) * sizeof(P386TableEntry));
    t->entries = ne;
    t->cap = new_cap;
    return 1;
}

static int find_entry(const P386Table *t, const P386Value *key) {
    uint32_t i;
    for (i = 0; i < t->len; i++) {
        if (t->entries[i].key.tag == P386_TAG_NIL) continue;
        if (value_eq(&t->entries[i].key, key)) return (int)i;
    }
    return -1;
}

void p386_table_get(const P386Table *t, const P386Value *key, P386Value *out) {
    int idx;
    out->value = 0;
    out->tag = P386_TAG_NIL;
    if (!t || key->tag == P386_TAG_NIL) return;
    idx = find_entry(t, key);
    if (idx >= 0) *out = t->entries[idx].val;
}

static void recompute_array_len(P386Table *t) {
    uint32_t n = 0;
    for (;;) {
        P386Value k;
        int idx;
        k.tag = P386_TAG_NUM;
        k.value = (int32_t)((n + 1) << 16);
        idx = find_entry(t, &k);
        if (idx < 0 || t->entries[idx].val.tag == P386_TAG_NIL) break;
        n++;
    }
    t->array_len = n;
}

void p386_table_set(P386Table *t, const P386Value *key, const P386Value *val) {
    int idx;
    if (!t || key->tag == P386_TAG_NIL) return;
    idx = find_entry(t, key);
    if (idx >= 0) {
        if (val->tag == P386_TAG_NIL) {
            /* tombstone */
            t->entries[idx].key.tag = P386_TAG_NIL;
            t->entries[idx].val.tag = P386_TAG_NIL;
        } else {
            t->entries[idx].val = *val;
        }
    } else {
        if (val->tag == P386_TAG_NIL) return;
        if (t->len == t->cap && !table_grow(t)) return;
        t->entries[t->len].key = *key;
        t->entries[t->len].val = *val;
        t->len++;
    }
    /* maintain array_len for integer keys (16.16 representation of 1,2,3...) */
    if (key->tag == P386_TAG_NUM && (((uint32_t)key->value & 0xffffu) == 0)) {
        int32_t k = key->value >> 16;
        if (k >= 1 && (uint32_t)k <= t->array_len + 1) {
            recompute_array_len(t);
        } else if (k >= 1 && val->tag == P386_TAG_NIL) {
            recompute_array_len(t);
        }
    }
}

uint32_t p386_table_len(const P386Table *t) {
    if (!t) return 0;
    return t->array_len;
}

int p386_table_next(const P386Table *t, const P386Value *key,
                    P386Value *out_key, P386Value *out_val) {
    uint32_t i = 0;
    out_key->value = 0;
    out_key->tag = P386_TAG_NIL;
    out_val->value = 0;
    out_val->tag = P386_TAG_NIL;
    if (!t) return 0;

    if (key->tag != P386_TAG_NIL) {
        int idx = find_entry(t, key);
        if (idx < 0) return 0;
        i = (uint32_t)idx + 1;
    }

    for (; i < t->len; i++) {
        if (t->entries[i].key.tag == P386_TAG_NIL) continue;
        if (t->entries[i].val.tag == P386_TAG_NIL) continue;
        *out_key = t->entries[i].key;
        *out_val = t->entries[i].val;
        return 1;
    }
    return 0;
}

P386Closure *p386_closure_new(uint32_t proto_index, const P386ProtoEntry *proto,
                              uint8_t n_upvalues) {
    size_t bytes = sizeof(P386Closure);
    P386Closure *c;
    if (n_upvalues > 0) {
        bytes += ((size_t)n_upvalues - 1U) * sizeof(P386Upvalue *);
    }
    c = (P386Closure *)calloc(1, bytes);
    if (!c) return 0;
    c->proto_index = proto_index;
    c->proto = proto;
    c->n_upvalues = n_upvalues;
    return c;
}

P386Upvalue *p386_upvalue_find_or_add(P386Upvalue **head, P386Value *slot) {
    P386Upvalue *uv;
    if (!head || !slot) return 0;
    uv = *head;
    while (uv) {
        if (uv->slot == slot) return uv;
        uv = uv->next_open;
    }
    uv = (P386Upvalue *)malloc(sizeof(P386Upvalue));
    if (!uv) return 0;
    uv->slot = slot;
    uv->closed.value = 0;
    uv->closed.tag = P386_TAG_NIL;
    uv->next_open = *head;
    *head = uv;
    return uv;
}

void p386_close_upvalues(P386Upvalue **head, P386Value *from_slot) {
    P386Upvalue *uv;
    P386Upvalue *prev;
    if (!head || !from_slot) return;
    prev = 0;
    uv = *head;
    while (uv) {
        if (uv->slot >= from_slot) {
            uv->closed = *uv->slot;
            uv->slot = &uv->closed;
            if (prev) prev->next_open = uv->next_open;
            else *head = uv->next_open;
            uv = (prev ? prev->next_open : *head);
            continue;
        }
        prev = uv;
        uv = uv->next_open;
    }
}
