#include <stdlib.h>
#include <string.h>

#include "pico386.h"

int p386_cart_load(P386_Cart *ctx, const char *filename) {
    int retval;

    debug_serial_printf("Loading cart %s\n", filename);

    if (load_png(filename) != 0)
        return -1;

    if (scan_cart() != 0 || load_data() != 0)
        return -2;

    /* cart_data is set by load_data() via the global in cart.c */
    ctx->cart_data = cart_data;

    ctx->lua_code = malloc(P8_CODE_ALLOC);
    if (!ctx->lua_code)
        return -3;

    /*
     * pico8_code_section_decompress returns:
     *   - 0 for raw/pxa formats on success
     *   - decompressed length for :c: format on success
     *   - 1 if :c: format data is corrupt (len > max_len)
     * In all success cases, lua_code is null-terminated.
     */
    pico8_code_section_decompress(
        ctx->cart_data + P8_CODE_OFFSET, ctx->lua_code, P8_CODE_MAX);

    if (ctx->lua_code[0] == '\0') {
        debug_serial_print("pico8_decomp: no code extracted\n");
        return -4;
    }

    debug_serial_printf("Lua code: %d bytes\n", strlen(ctx->lua_code));
    return 0;
}

int p386_cart_compile(P386_Cart *ctx) {
    const unsigned char *bc;
    unsigned long bc_len;

    if (!ctx->lua_code)
        return -1;

    debug_serial_printf("p8_compile: compiling %d bytes...\n", strlen(ctx->lua_code));
    ctx->program = p8_compile(ctx->lua_code, strlen(ctx->lua_code));

    if (!ctx->program) {
        debug_serial_print("p8_compile: FAIL\n");
        return -2;
    }

    bc_len = p8_program_bytecode(ctx->program, &bc);
    debug_serial_printf("p8_compile: OK, %d bytes bytecode, %d constants, %d protos\n",
        (int)bc_len, (int)p8_program_num_constants(ctx->program),
        (int)p8_program_num_protos(ctx->program));

    return 0;
}

void p386_cart_free(P386_Cart *ctx) {
    if (ctx->program) {
        p8_free_program(ctx->program);
        ctx->program = 0;
    }
    if (ctx->lua_code) {
        free(ctx->lua_code);
        ctx->lua_code = 0;
    }
    /* unload() frees cart_data and the PNG chunks */
    unload();
    ctx->cart_data = 0;
}
