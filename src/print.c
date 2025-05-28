#include "serial.h"

void debug_serial_print(const char *str) {
    while (*str) {
        debug_serial_putchar(*str);
        ++str;
    }
}
