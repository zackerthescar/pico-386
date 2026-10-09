/*
 * pico386 VM heap objects: strings, tables, intern table.
 *
 * Pure C; no asm, no DOS calls. Designed to be host-testable.
 * Allocation: p386_gc_alloc; the collector in p386_gc.c frees objects.
 */

#include <stdlib.h>
#include <string.h>
#include "p386_obj.h"
#include "p386_gc.h"

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

/* Drop the intern table (p386_gc_reset frees the strings). */
void p386_string_intern_reset(void) {
    free(g_intern);
    g_intern = 0;
    g_intern_cap = 0;
    g_intern_n = 0;
}

/* Called by the collector after marking: remove unmarked strings. The
 * table uses linear probing, so it is rebuilt instead of punching holes. */
void p386_string_intern_sweep(void) {
    uint32_t i, n = 0, mask = g_intern_cap - 1;
    InternBucket *nb;
    if (!g_intern) return;
    nb = (InternBucket *)calloc(g_intern_cap, sizeof(InternBucket));
    if (!nb) {
        /* No memory for a new table: keep every string alive instead. */
        for (i = 0; i < g_intern_cap; i++) {
            if (g_intern[i].s) P386_GC_HDR(g_intern[i].s)->marked = 1;
        }
        return;
    }
    for (i = 0; i < g_intern_cap; i++) {
        P386String *e = g_intern[i].s;
        uint32_t j;
        if (!e || !P386_GC_HDR(e)->marked) continue;
        j = e->hash & mask;
        while (nb[j].s) j = (j + 1) & mask;
        nb[j].s = e;
        n++;
    }
    free(g_intern);
    g_intern = nb;
    g_intern_n = n;
}

static P386String *string_alloc(const char *data, uint32_t len, uint32_t hash) {
    P386String *s = (P386String *)p386_gc_alloc(P386_GC_STR, (uint32_t)sizeof(P386String) + len);
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
    P386_GC_HDR(s)->flags |= P386_GC_INTERNED;
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

/* Byte-wise (unsigned) lexicographic compare. A prefix sorts first.
 * A null pointer counts as the empty string. */
int p386_string_cmp(const P386String *a, const P386String *b) {
    uint32_t la = a ? a->len : 0;
    uint32_t lb = b ? b->len : 0;
    uint32_t n = la < lb ? la : lb;
    if (n) {
        int r = memcmp(a->data, b->data, n);
        if (r) return r;
    }
    return la < lb ? -1 : (la > lb ? 1 : 0);
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
/*
 * A table has two parts, as in Lua:
 *   - array part: t[1..asize] in arr[0..asize-1]. arr[asize-1] is never nil
 *     (setting the last element to nil trims asize), so #t is asize.
 *     Elements inside can be nil (holes).
 *   - hash part: open addressing with linear probing, hcap slots (a power of
 *     two, or 0 when there is none). An empty slot has key tag NIL.
 *
 * t[k] = nil for a key in the hash part keeps the key with a nil value, so
 * next() still finds that key while pairs() runs (Lua allows clearing
 * fields during a traversal). Such slots count as used until a rehash. The
 * collector turns them into DEAD_KEY slots: the key is no longer a
 * reference, but its bits stay for identity (see p386_table_traverse).
 */

#define DEAD_KEY 0xFFu              /* key tag of a dead slot (tables only) */
#define HASH_MIN 4

static int is_array_key(const P386Value *k, uint32_t *idx) {
    if (k->tag != P386_TAG_NUM || (k->value & 0xffff) || k->value <= 0) return 0;
    *idx = (uint32_t)(k->value >> 16) - 1u;     /* 0-based */
    return 1;
}

static uint32_t key_hash(const P386Value *k) {
    uint32_t h;
    if (k->tag == P386_TAG_STR) return ((const P386String *)(uintptr_t)k->value)->hash;
    h = (uint32_t)k->value ^ (k->tag * 0x9e3779b9u);
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

static int key_eq(const P386Value *a, const P386Value *b) {
    if (a->tag != b->tag) return 0;
    if (a->value == b->value) return 1;
    /* Strings are interned, so equal strings have one pointer. Only a
     * string made without the intern table (out of memory) needs this. */
    return a->tag == P386_TAG_STR &&
           p386_string_eq((const P386String *)(uintptr_t)a->value,
                          (const P386String *)(uintptr_t)b->value);
}

/* Hash slot of key, or NULL. dead_ok also matches a DEAD_KEY slot with the
 * same key bits (for next() after a collection). */
static P386TableEntry *hash_find(const P386Table *t, const P386Value *key, int dead_ok) {
    uint32_t mask, i;
    if (!t->hcap) return 0;
    mask = t->hcap - 1;
    i = key_hash(key) & mask;
    for (;;) {
        P386TableEntry *e = &t->hash[i];
        if (e->key.tag == P386_TAG_NIL) return 0;
        if (key_eq(&e->key, key)) return e;
        if (dead_ok && e->key.tag == DEAD_KEY && e->key.value == key->value) return e;
        i = (i + 1) & mask;
    }
}

/* Put a key that is not in the table into a free slot (no resize). */
static P386TableEntry *hash_insert_new(P386Table *t, const P386Value *key) {
    uint32_t mask = t->hcap - 1, i = key_hash(key) & mask;
    while (t->hash[i].key.tag != P386_TAG_NIL && t->hash[i].key.tag != DEAD_KEY) {
        i = (i + 1) & mask;
    }
    if (t->hash[i].key.tag == P386_TAG_NIL) t->hused++;
    t->hash[i].key = *key;
    return &t->hash[i];
}

/* Rebuild the hash part for its live entries (drops nil and dead slots). */
static int hash_resize(P386Table *t, uint32_t live_extra) {
    P386TableEntry *old = t->hash;
    uint32_t old_cap = t->hcap, live = 0, cap = HASH_MIN, i;
    for (i = 0; i < old_cap; i++) {
        if (old[i].key.tag != P386_TAG_NIL && old[i].key.tag != DEAD_KEY &&
            old[i].val.tag != P386_TAG_NIL) live++;
    }
    live += live_extra;
    while (cap * 3 < live * 4 + 4) cap *= 2;      /* load factor < 3/4 */
    t->hash = (P386TableEntry *)calloc(cap, sizeof(P386TableEntry));
    if (!t->hash) {
        t->hash = old;
        return 0;
    }
    t->hcap = cap;
    t->hused = 0;
    for (i = 0; i < old_cap; i++) {
        if (old[i].key.tag != P386_TAG_NIL && old[i].key.tag != DEAD_KEY &&
            old[i].val.tag != P386_TAG_NIL) {
            hash_insert_new(t, &old[i].key)->val = old[i].val;
        }
    }
    free(old);
    p386_gc_account((int32_t)((cap - old_cap) * sizeof(P386TableEntry)));
    return 1;
}

static int array_reserve(P386Table *t, uint32_t n) {
    uint32_t cap = t->acap ? t->acap : 4;
    P386Value *na;
    if (n <= t->acap) return 1;
    while (cap < n) cap *= 2;
    na = (P386Value *)realloc(t->arr, cap * sizeof(P386Value));
    if (!na) return 0;
    p386_gc_account((int32_t)((cap - t->acap) * sizeof(P386Value)));
    t->arr = na;
    t->acap = cap;
    return 1;
}

/* t[asize+1] = v (v not nil). Then move asize+1, asize+2, ... from the hash
 * part into the array part while they exist. */
static void array_append(P386Table *t, const P386Value *v) {
    P386Value k;
    P386TableEntry *e;
    if (!array_reserve(t, t->asize + 1)) return;
    t->arr[t->asize++] = *v;
    k.tag = P386_TAG_NUM;
    for (;;) {
        k.value = (int32_t)((t->asize + 1) << 16);
        e = hash_find(t, &k, 0);
        if (!e || e->val.tag == P386_TAG_NIL) break;
        if (!array_reserve(t, t->asize + 1)) break;
        t->arr[t->asize++] = e->val;
        e->val.value = 0;
        e->val.tag = P386_TAG_NIL;
    }
}

P386Table *p386_table_new(uint32_t array_hint, uint32_t hash_hint) {
    /* Zeroed: no parts. A failed part allocation leaves a valid table. */
    P386Table *t = (P386Table *)p386_gc_alloc(P386_GC_TAB, (uint32_t)sizeof(P386Table));
    if (!t) return 0;
    if (array_hint) array_reserve(t, array_hint);
    if (hash_hint) hash_resize(t, hash_hint);
    return t;
}

void p386_table_get(const P386Table *t, const P386Value *key, P386Value *out) {
    uint32_t idx;
    const P386TableEntry *e;
    out->value = 0;
    out->tag = P386_TAG_NIL;
    if (!t) return;
    if (is_array_key(key, &idx) && idx < t->asize) {
        *out = t->arr[idx];
        return;
    }
    e = hash_find(t, key, 0);
    if (e) *out = e->val;
}

void p386_table_set(P386Table *t, const P386Value *key, const P386Value *val) {
    uint32_t idx;
    P386TableEntry *e;
    if (!t || key->tag == P386_TAG_NIL) return;
    if (is_array_key(key, &idx)) {
        if (idx < t->asize) {
            t->arr[idx] = *val;
            if (val->tag == P386_TAG_NIL && idx + 1 == t->asize) {
                while (t->asize && t->arr[t->asize - 1].tag == P386_TAG_NIL) t->asize--;
            }
            return;
        }
        if (idx == t->asize) {
            if (val->tag != P386_TAG_NIL) {
                /* The key can be in the hash part (set when it was not
                 * next): clear it there first. */
                e = hash_find(t, key, 0);
                if (e) e->val.tag = P386_TAG_NIL, e->val.value = 0;
                array_append(t, val);
            }
            return;
        }
    }
    e = hash_find(t, key, 0);
    if (e) {
        e->val = *val;
        return;
    }
    if (val->tag == P386_TAG_NIL) return;
    if ((t->hused + 1) * 4 > t->hcap * 3 && !hash_resize(t, 1)) return;
    hash_insert_new(t, key)->val = *val;
}

uint32_t p386_table_len(const P386Table *t) {
    return t ? t->asize : 0;
}

int p386_table_next(const P386Table *t, const P386Value *key,
                    P386Value *out_key, P386Value *out_val) {
    uint32_t i = 0, idx;
    out_key->value = 0;
    out_key->tag = P386_TAG_NIL;
    out_val->value = 0;
    out_val->tag = P386_TAG_NIL;
    if (!t) return 0;

    /* Position after key: array index, then hash slot. */
    if (key->tag != P386_TAG_NIL) {
        if (is_array_key(key, &idx) && idx < t->asize) {
            i = idx + 1;
        } else {
            const P386TableEntry *e = hash_find(t, key, 1);
            if (e) {
                i = t->asize + (uint32_t)(e - t->hash) + 1;
            } else if (is_array_key(key, &idx) && idx < t->acap) {
                i = t->asize;       /* the array part was trimmed past it */
            } else {
                return 0;           /* unknown key */
            }
        }
    }
    for (; i < t->asize; i++) {
        if (t->arr[i].tag == P386_TAG_NIL) continue;
        out_key->tag = P386_TAG_NUM;
        out_key->value = (int32_t)((i + 1) << 16);
        *out_val = t->arr[i];
        return 1;
    }
    for (i -= t->asize; i < t->hcap; i++) {
        const P386TableEntry *e = &t->hash[i];
        if (e->key.tag == P386_TAG_NIL || e->key.tag == DEAD_KEY) continue;
        if (e->val.tag == P386_TAG_NIL) continue;
        *out_key = e->key;
        *out_val = e->val;
        return 1;
    }
    return 0;
}

/* Collector: mark the contents. Slots with a nil value become DEAD_KEY:
 * the key is not marked (it may be freed), and lookups never compare
 * through it; next() still matches its bits. */
void p386_table_traverse(P386Table *t) {
    uint32_t i;
    for (i = 0; i < t->asize; i++) p386_gc_mark_value(&t->arr[i]);
    for (i = 0; i < t->hcap; i++) {
        P386TableEntry *e = &t->hash[i];
        if (e->key.tag == P386_TAG_NIL || e->key.tag == DEAD_KEY) continue;
        if (e->val.tag == P386_TAG_NIL) {
            e->key.tag = DEAD_KEY;
            continue;
        }
        p386_gc_mark_value(&e->key);
        p386_gc_mark_value(&e->val);
    }
    p386_gc_mark_object(t->metatable);
}

/* Collector: free the parts of a table. Returns the bytes they used. */
uint32_t p386_table_free_parts(P386Table *t, int poison) {
    uint32_t abytes = t->acap * (uint32_t)sizeof(P386Value);
    uint32_t hbytes = t->hcap * (uint32_t)sizeof(P386TableEntry);
    if (t->arr) {
        if (poison) memset(t->arr, 0xDD, abytes);
        free(t->arr);
    }
    if (t->hash) {
        if (poison) memset(t->hash, 0xDD, hbytes);
        free(t->hash);
    }
    t->arr = 0;
    t->hash = 0;
    return abytes + hbytes;
}

P386Closure *p386_closure_new(uint32_t proto_index, const P386ProtoEntry *proto,
                              uint8_t n_upvalues) {
    size_t bytes = sizeof(P386Closure);
    P386Closure *c;
    if (n_upvalues > 0) {
        bytes += ((size_t)n_upvalues - 1U) * sizeof(P386Upvalue *);
    }
    c = (P386Closure *)p386_gc_alloc(P386_GC_CLOSURE, (uint32_t)bytes);
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
    uv = (P386Upvalue *)p386_gc_alloc(P386_GC_UPVAL, (uint32_t)sizeof(P386Upvalue));
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
