/*
 * crt_shim.c - Bridge Rust ELF symbol references to Watcom C runtime
 *
 * wlink can't resolve ELF undefined symbols against OMF libraries.
 * We provide:
 *   wc_malloc/wc_free/wc_realloc - wrappers that call clib's malloc/free/realloc
 *   memcmp/memcpy/memmove - direct implementations (no clib call to avoid recursion)
 *
 * #pragma aux "name" controls the exact OMF symbol name emitted.
 */

#include <stdlib.h>

/* Allocator wrappers - unique names so no conflict with clib */
#pragma aux shim_malloc "wc_malloc";
void *shim_malloc(unsigned n) { return malloc(n); }

#pragma aux shim_free "wc_free";
void shim_free(void *p) { free(p); }

#pragma aux shim_realloc "wc_realloc";
void *shim_realloc(void *p, unsigned n) { return realloc(p, n); }

/* mem* implementations - must use the EXACT names Rust expects.
 * Implemented inline to avoid calling clib's versions (which would
 * cause symbol conflicts). */

#pragma aux shim_memcmp "memcmp";
int shim_memcmp(const void *a, const void *b, unsigned n) {
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    unsigned i;
    for (i = 0; i < n; i++) {
        if (p[i] != q[i]) return p[i] < q[i] ? -1 : 1;
    }
    return 0;
}

#pragma aux shim_memcpy "memcpy";
void *shim_memcpy(void *d, const void *s, unsigned n) {
    unsigned char *dp = (unsigned char *)d;
    const unsigned char *sp = (const unsigned char *)s;
    unsigned i;
    for (i = 0; i < n; i++) dp[i] = sp[i];
    return d;
}

#pragma aux shim_memmove "memmove";
void *shim_memmove(void *d, const void *s, unsigned n) {
    unsigned char *dp = (unsigned char *)d;
    const unsigned char *sp = (const unsigned char *)s;
    if (dp < sp) {
        unsigned i;
        for (i = 0; i < n; i++) dp[i] = sp[i];
    } else {
        unsigned i = n;
        while (i-- > 0) dp[i] = sp[i];
    }
    return d;
}
