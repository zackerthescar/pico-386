#include <conio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "serial.h"

void debug_serial_init() {
    outp(0x2F8 + 3, 0x80);
    outp(0x2F8, 0x01);
    outp(0x2F9, 0x00);
    outp(0x2F8 + 3, 0x03);
    outp(0x2F8 + 2, 0xC7);
}

void debug_serial_putchar(char c) {
    while(!(inp(0x2F8 + 5) & 0x20)) {

    }
    outp(0x2F8, c);
}

void debug_serial_print(const char *str) {
    while (*str) {
        debug_serial_putchar(*str);
        ++str;
    }
}

// This is really more of an exercise in "can we write a printf."
// 
int debug_serial_printf(const char *format, ...) {
    const char *p = format;
    char buffer[33];
    char c; 
    int num; 
    const char *str;
    va_list args;
    va_start(args, format);
    while (*p) {
        if (*p == '%' && *(p + 1)) {
            p++;
            switch (*p) {
                case 'c': {
                    c = (char) va_arg(args, int);
                    debug_serial_putchar(c);
                    break;
                }
                case 's': {
                    str = va_arg(args, const char*);
                    if (str) {
                        debug_serial_print(str);
                    } else {
                        debug_serial_print("(null)");
                    }
                    break;
                }
                case 'i':
                case 'd': {
                    num = va_arg(args, int);
                    itoa(num, buffer, 10);
                    debug_serial_print(buffer);
                    break;
                }
                case 'X': // to-do: uppercase variant
                case 'x': {
                    num = va_arg(args, int);
                    itoa(num, buffer, 16);
                    debug_serial_print(buffer);
                    break;
                }
                case 'p': {
                    debug_serial_putchar('0');
                    debug_serial_putchar('x');
                    num = (int) va_arg(args, void *);
                    itoa(num, buffer, 16);
                    debug_serial_print(buffer);
                    break;
                }
                // to-do: other variants
                default: {
                    debug_serial_putchar('%');
                    debug_serial_putchar(*p);
                    break;
                }
            }
        } else {
            debug_serial_putchar(*p);
        }
        p++;
    }
    va_end(args);
    return 0;
}
