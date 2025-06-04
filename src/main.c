#include <stdio.h>
#include <unistd.h>

#include "serial.h"
#include "vga.h"
#include "cart.h"


void main(int argc, char *argv[]) {
    int x;
    debug_serial_init();
    debug_serial_print("Going into VGA int 13h...\n");
    vga_init();
    debug_serial_print("Setting some values...\n");
    draw_bound_box();
    sleep(1);
    if (argc > 1) {
        debug_serial_printf("Loading cart %s\n", argv[1]);
        load_png(argv[1]);
        scan_cart();
        debug_serial_print("Unloading cart...\n");
        unload();
    }
    debug_serial_print("Returning from VGA int 13h...\n");
    vga_ret();
    return;
}
