#include <unistd.h>
#include <string.h>

#include "pico386.h"
#include "vga.h"
#include "mem.h"

P8Ram p8_ram;

void main(int argc, char *argv[]) {
    P386_Cart cart = {0};

    debug_serial_init();
    debug_serial_print("Initializing VGA Mode X 320x400...\n");

    /* Zero PICO-8 RAM */
    memset(&p8_ram, 0, sizeof(p8_ram));

    vga_init();

    if (argc > 1) {
        if (p386_cart_load(&cart, argv[1]) == 0) {
            debug_serial_printf("%s\n", cart.lua_code);
            p386_cart_compile(&cart);

            /* Copy cart sprite/map/flag/sfx/music data into PICO-8 RAM */
            if (cart.cart_data) {
                memcpy(p8_ram.raw, cart.cart_data, 0x4300);
            }

            /* Test blit: render whatever is in the screen buffer */
            vga_blit(p8_ram.mem.screen);
            vga_flip();
        }
        debug_serial_print("Unloading cart...\n");
        p386_cart_free(&cart);
    }

    sleep(3);
    debug_serial_print("Returning to text mode...\n");
    vga_ret();
}
