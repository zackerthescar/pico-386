#ifndef _SERIAL_H

#define _SERIAL_H

void debug_serial_init();
void debug_serial_putchar(char c);
void debug_serial_print(const char *);
int debug_serial_printf(const char *, ...);

#endif
