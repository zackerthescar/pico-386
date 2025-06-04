#include <stdio.h>
#include <unistd.h>

#include "serial.h"
#include "vga.h"
#include "cart.h"


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

        }
        debug_serial_print("Unloading cart...\n");
        unload();
    }
    sleep(1);
    debug_serial_print("Returning from VGA int 13h...\n");
    vga_ret();
    return;
}
