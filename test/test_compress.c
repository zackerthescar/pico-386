#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "test.h"
#include "pico8.h"

/*
 * Tests for the PICO-8 code decompression routines.
 * Tests decompress_mini() (legacy :c: format) and
 * pico8_code_section_decompress() (unified API).
 */

/* ── Legacy :c: format (decompress_mini) ── */

TEST(decompress_mini_empty) {
    /* A :c: stream encoding an empty string (length 0) */
    uint8_t compressed[] = {
        ':', 'c', ':', 0,   /* header */
        0, 0,               /* uncompressed length = 0 */
        0, 0                /* compressed length (unused by decompressor) */
    };
    uint8_t out[64];
    memset(out, 0xFF, sizeof(out));

    decompress_mini(compressed, out, 64);

    /* Output buffer should be zeroed by decompressor */
    ASSERT_EQ(0, out[0]);
    PASS();
}

TEST(decompress_mini_literals_only) {
    /*
     * The literal table is: "^\n 0123456789abcdef..."
     * Index 1 = '\n', Index 2 = ' ', Index 3 = '0', ...
     * Index 0 = next byte is a raw literal
     *
     * Encode "abc" using the literal table:
     *   'a' is at index 13 (offset: ^=0, \n=1, ' '=2, '0'..'9'=3..12, 'a'=13)
     *   'b' = 14, 'c' = 15
     */
    uint8_t compressed[] = {
        ':', 'c', ':', 0,   /* header */
        0, 3,               /* uncompressed length = 3 */
        0, 0,               /* compressed length */
        13, 14, 15          /* literals: a, b, c */
    };
    uint8_t out[64];
    memset(out, 0xFF, sizeof(out));

    decompress_mini(compressed, out, 64);

    ASSERT_EQ('a', out[0]);
    ASSERT_EQ('b', out[1]);
    ASSERT_EQ('c', out[2]);
    ASSERT_EQ(0, out[3]);
    PASS();
}

TEST(decompress_mini_raw_literal) {
    /*
     * Characters not in the literal table are encoded as:
     *   0x00 <raw_byte>
     * The '=' character is not in the literal table.
     */
    uint8_t compressed[] = {
        ':', 'c', ':', 0,
        0, 1,               /* length = 1 */
        0, 0,
        0, '='              /* raw literal: '=' */
    };
    uint8_t out[64];
    memset(out, 0xFF, sizeof(out));

    decompress_mini(compressed, out, 64);

    ASSERT_EQ('=', out[0]);
    ASSERT_EQ(0, out[1]);
    PASS();
}

TEST(decompress_mini_corrupt_length) {
    /* Length claims 0xFFFF bytes — should be rejected (> max_len) */
    uint8_t compressed[] = {
        ':', 'c', ':', 0,
        0xFF, 0xFF,         /* uncompressed length = 65535 */
        0, 0,
    };
    uint8_t out[64];
    int retval;

    retval = decompress_mini(compressed, out, 64);

    /* Should return 1 (corrupt data) when len > max_len */
    ASSERT_EQ(1, retval);
    PASS();
}

/* ── Unified decompression API ── */

TEST(decompress_unknown_format) {
    /*
     * When data doesn't match ":c:" or "pxa" headers, the decompressor
     * treats it as raw text and copies 0x3d00 bytes from input to output.
     * We must provide buffers large enough to survive that.
     */
    uint8_t *input;
    uint8_t *out;

    input = malloc(0x4000);
    out = malloc(0x4000);
    ASSERT_NOT_NULL(input);
    ASSERT_NOT_NULL(out);

    memset(input, 0, 0x4000);
    memset(out, 0, 0x4000);
    input[0] = 0xDE;
    input[1] = 0xAD;

    pico8_code_section_decompress(input, out, 0x3d00);

    /* Raw path copies input verbatim, null-terminates at 0x3d00 */
    ASSERT_EQ(0xDE, out[0]);
    ASSERT_EQ(0xAD, out[1]);
    ASSERT_EQ(0, out[0x3d00]);

    free(input);
    free(out);
    PASS();
}

/* ── Round-trip: compress then decompress ── */

TEST(compress_roundtrip_simple) {
    /*
     * compress_mini returns raw input if compressed is larger,
     * so we need input long enough to actually trigger compression.
     * Use a realistic PICO-8 snippet with repeated patterns.
     */
    const char *input =
        "function _init()\n"
        " x = 0\n y = 0\n"
        " for i = 0, 10 do\n"
        "  x = x + 1\n  y = y + 1\n"
        " end\n"
        "end\n"
        "function _update()\n"
        " x = x + 1\n y = y + 1\n"
        "end\n";
    int input_len = strlen(input);
    uint8_t *compressed;
    uint8_t *decompressed;
    int comp_len;

    compressed = malloc(0x10001);
    decompressed = malloc(0x10001);
    ASSERT_NOT_NULL(compressed);
    ASSERT_NOT_NULL(decompressed);

    memset(compressed, 0, 0x10001);
    memset(decompressed, 0, 0x10001);

    comp_len = compress_mini((uint8_t *)input, compressed, input_len);
    ASSERT_TRUE(comp_len > 0);

    decompress_mini(compressed, decompressed, 0x10000);

    ASSERT_STR_EQ(input, (const char *)decompressed);

    free(compressed);
    free(decompressed);
    PASS();
}

TEST(compress_roundtrip_repeated) {
    /* Repeated patterns should trigger block copies */
    const char *input = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    int input_len = strlen(input);
    uint8_t compressed[256];
    uint8_t decompressed[256];
    int comp_len;

    memset(compressed, 0, sizeof(compressed));
    memset(decompressed, 0, sizeof(decompressed));

    comp_len = compress_mini((uint8_t *)input, compressed, input_len);
    ASSERT_TRUE(comp_len > 0);
    /* Repeated data should compress smaller than input */
    ASSERT_TRUE(comp_len < input_len);

    decompress_mini(compressed, decompressed, 256);

    ASSERT_STR_EQ(input, (const char *)decompressed);
    PASS();
}
