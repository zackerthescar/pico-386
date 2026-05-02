/*
 * Host-runnable tests for p386_obj.c (strings + tables).
 *
 * On the 32-bit DOS target, P386Value.value (int32_t) holds a real pointer.
 * On a 64-bit host we wrap p386_obj.c's malloc/calloc/realloc so all heap
 * allocations come from a region we mmap'd with MAP_32BIT, keeping pointers
 * representable in 32 bits.
 *
 * The wrapping is done by compiling this TU with macros that redirect the
 * three allocator calls inside p386_obj.c, and then including the source.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static void *host_alloc(size_t n);
static void *host_calloc(size_t c, size_t sz);
static void *host_realloc(void *p, size_t n);
static void  host_free(void *p);

/* Redirect the three allocators used inside p386_obj.c. */
#define malloc  host_alloc
#define calloc  host_calloc
#define realloc host_realloc
#define free    host_free
#include "../src/p386_obj.c"
#undef malloc
#undef calloc
#undef realloc
#undef free

static unsigned char *pool_base = NULL;
static size_t          pool_off = 0;
static const size_t    POOL = 8u * 1024u * 1024u;

static void pool_init(void) {
    if (pool_base) return;
    pool_base = mmap(NULL, POOL, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT, -1, 0);
    if (pool_base == MAP_FAILED) { perror("mmap MAP_32BIT"); abort(); }
}

static void *host_alloc(size_t n) {
    pool_init();
    n = (n + 7u) & ~(size_t)7u;
    if (pool_off + n > POOL) return NULL;
    void *p = pool_base + pool_off;
    pool_off += n;
    return p;
}
static void *host_calloc(size_t c, size_t sz) {
    size_t n = c * sz;
    void *p = host_alloc(n);
    if (p) memset(p, 0, n);
    return p;
}
static void *host_realloc(void *p, size_t n) {
    void *q = host_alloc(n);
    if (q && p) memcpy(q, p, n);   /* over-copies, but pool is read-safe */
    return q;
}
static void host_free(void *p) { (void)p; }

/* ----- tests ----- */

static int failures = 0;
static int total = 0;

#define CHECK(cond) do { \
    total++; \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static P386Value v_num(int32_t whole) {
    P386Value v;
    v.value = whole << 16;
    v.tag = P386_TAG_NUM;
    return v;
}
static P386Value v_str(P386String *s) {
    P386Value v;
    v.value = (int32_t)(uintptr_t)s;
    v.tag = P386_TAG_STR;
    return v;
}
static P386Value v_nil(void) {
    P386Value v;
    v.value = 0;
    v.tag = P386_TAG_NIL;
    return v;
}

static void test_intern(void) {
    P386String *a = p386_string_intern("hello", 5);
    P386String *b = p386_string_intern("hello", 5);
    P386String *c = p386_string_intern("world", 5);
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a->len == 5);
    CHECK(memcmp(a->data, "hello", 5) == 0);
    CHECK(a->data[5] == 0);
    CHECK(p386_string_eq(a, b));
    CHECK(!p386_string_eq(a, c));
}

static void test_num_to_string(void) {
    P386String *s;
    s = p386_num_to_string(0);                     CHECK(strcmp(s->data, "0") == 0);
    s = p386_num_to_string(1 << 16);               CHECK(strcmp(s->data, "1") == 0);
    s = p386_num_to_string(-(1 << 16));            CHECK(strcmp(s->data, "-1") == 0);
    s = p386_num_to_string((1 << 16) + (1 << 15)); CHECK(strcmp(s->data, "1.5") == 0);
    s = p386_num_to_string(42 << 16);              CHECK(strcmp(s->data, "42") == 0);
}

static void test_concat(void) {
    P386Value a = v_str(p386_string_intern("foo", 3));
    P386Value b = v_str(p386_string_intern("bar", 3));
    P386Value n = v_num(7);
    P386String *out;

    out = p386_value_concat(&a, &b);
    CHECK(out && out->len == 6 && memcmp(out->data, "foobar", 6) == 0);
    /* same args -> same interned pointer */
    CHECK(out == p386_value_concat(&a, &b));
    /* num coercion */
    out = p386_value_concat(&a, &n);
    CHECK(out && strcmp(out->data, "foo7") == 0);
    out = p386_value_concat(&n, &a);
    CHECK(out && strcmp(out->data, "7foo") == 0);
    /* type error */
    {
        P386Value bad; bad.tag = P386_TAG_BOOL; bad.value = 1;
        CHECK(p386_value_concat(&a, &bad) == NULL);
    }
}

static void test_table_basic(void) {
    P386Table *t = p386_table_new(0, 0);
    P386Value k1 = v_num(1), k2 = v_num(2), k3 = v_num(3);
    P386Value v10 = v_num(10), v20 = v_num(20), v30 = v_num(30);
    P386Value out;
    CHECK(t != NULL);
    p386_table_set(t, &k1, &v10);
    p386_table_set(t, &k2, &v20);
    p386_table_set(t, &k3, &v30);
    CHECK(p386_table_len(t) == 3);

    p386_table_get(t, &k2, &out);
    CHECK(out.tag == P386_TAG_NUM && out.value == (20 << 16));

    {
        P386Value miss = v_num(99);
        p386_table_get(t, &miss, &out);
        CHECK(out.tag == P386_TAG_NIL);
    }
    {
        P386Value nilv = v_nil();
        p386_table_set(t, &k2, &nilv);
        p386_table_get(t, &k2, &out);
        CHECK(out.tag == P386_TAG_NIL);
        CHECK(p386_table_len(t) == 1);
    }
}

static void test_table_string_keys(void) {
    P386Table *t = p386_table_new(0, 0);
    P386Value kfoo = v_str(p386_string_intern("foo", 3));
    P386Value kfoo2 = v_str(p386_string_intern("foo", 3));
    P386Value kbar = v_str(p386_string_intern("bar", 3));
    P386Value v42 = v_num(42), v99 = v_num(99);
    P386Value out;
    p386_table_set(t, &kfoo, &v42);
    p386_table_set(t, &kbar, &v99);
    p386_table_get(t, &kfoo2, &out);
    CHECK(out.tag == P386_TAG_NUM && out.value == (42 << 16));
    p386_table_set(t, &kfoo, &v99);
    p386_table_get(t, &kfoo, &out);
    CHECK(out.value == (99 << 16));
    CHECK(p386_table_len(t) == 0);
}

static void test_table_grow(void) {
    P386Table *t = p386_table_new(0, 0);
    int i;
    P386Value out;
    for (i = 1; i <= 32; i++) {
        P386Value k = v_num(i), v = v_num(i * 100);
        p386_table_set(t, &k, &v);
    }
    CHECK(p386_table_len(t) == 32);
    for (i = 1; i <= 32; i++) {
        P386Value k = v_num(i);
        p386_table_get(t, &k, &out);
        CHECK(out.tag == P386_TAG_NUM && out.value == ((i * 100) << 16));
    }
}

int main(void) {
    test_intern();
    test_num_to_string();
    test_concat();
    test_table_basic();
    test_table_string_keys();
    test_table_grow();
    if (failures) {
        fprintf(stderr, "\n%d/%d FAILED\n", failures, total);
        return 1;
    }
    printf("%d/%d PASSED\n", total - failures, total);
    return 0;
}
