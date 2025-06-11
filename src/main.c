#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "serial.h"
#include "vga.h"
#include "cart.h"
#include "pico8.h"

uint8_t *cart_data;
uint8_t *lua_code;

void main(int argc, char *argv[]) {
    int retval;
    debug_serial_init();
    debug_serial_print("Going into VGA int 13h...\n");
    vga_init();
    debug_serial_print("Drawing PICO-8 Window\n");
    draw_bound_box();
    if (argc > 1) {
        debug_serial_printf("Loading cart %s\n", argv[1]);
        load_png(argv[1]);
        if (!scan_cart() && !load_data()) {
            debug_serial_printf("Game data at %p\n", cart_data);
            lua_code = malloc(0x10001);
            retval = pico8_code_section_decompress(cart_data + 0x4300, lua_code, 0x10000);
            if (retval) {
                debug_serial_printf("pico8_decomp error: %d\n", retval);
            };
            debug_serial_printf("%s\n", lua_code);
        }
        debug_serial_print("Unloading cart...\n");
        if (lua_code) free(lua_code);
        unload();
    }
    sleep(1);
    debug_serial_print("Returning from VGA int 13h...\n");
    vga_ret();
    return;
}
