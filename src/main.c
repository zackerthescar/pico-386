#include <stdio.h>
#include <unistd.h>

#include "serial.h"
#include "vga.h"


void main(int argc, char *argv[]) {
    int x;
    debug_serial_init();
    debug_serial_print("Going into VGA int 13h...\n");
    vga_init();
    sleep(5);
    debug_serial_print("Setting some values...\n");
    for (x = 0; x < 200; x++) {
        vga_set(x, 128, 0x0A);
        vga_set(x, 129, 0x0A);
        vga_set(x, 130, 0x0A);
    }
    sleep(5);
    debug_serial_print("Returning from VGA int 13h...\n");
    vga_ret();
    return;
}
