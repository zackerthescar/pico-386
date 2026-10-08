#include <dos.h>
#include <conio.h>
#include "timer.h"

#define PIT_CLOCK     1193182UL
#define PIT_CH0       0x40
#define PIT_CMD       0x43
#define PIT_MODE3_CH0 0x36            /* ch 0, lo/hi byte, square wave */
#define PIC1_CMD      0x20
#define PIC_EOI       0x20
#define BIOS_PERIOD   0x10000UL        /* BIOS rate: divisor 65536 */

#define TIMER_DIVISOR ((uint16_t)((PIT_CLOCK + TIMER_HZ / 2) / TIMER_HZ))

static void (__interrupt __far *old_int8)(void);
static volatile uint32_t ticks;
static volatile uint32_t bios_acc;
static int installed;

static void pit_set_divisor(uint16_t div) {
    outp(PIT_CMD, PIT_MODE3_CH0);
    outp(PIT_CH0, div & 0xFF);
    outp(PIT_CH0, div >> 8);
}

/* See kbd.c: ISRs run on the extender's stack. */
#pragma off (check_stack)
static void __interrupt __far timer_isr(void) {
    ticks++;
    bios_acc += TIMER_DIVISOR;
    if (bios_acc >= BIOS_PERIOD) {
        bios_acc -= BIOS_PERIOD;
        _chain_intr(old_int8);          /* BIOS handler sends the EOI */
    }
    outp(PIC1_CMD, PIC_EOI);
}
#pragma on (check_stack)

void timer_init(void) {
    if (installed) return;
    ticks = 0;
    bios_acc = 0;
    old_int8 = _dos_getvect(0x08);
    _disable();
    _dos_setvect(0x08, timer_isr);
    pit_set_divisor(TIMER_DIVISOR);
    _enable();
    installed = 1;
}

void timer_shutdown(void) {
    if (!installed) return;
    _disable();
    pit_set_divisor(0);                 /* 0 = 65536 = 18.2 Hz */
    _dos_setvect(0x08, old_int8);
    _enable();
    installed = 0;
}

uint32_t timer_ticks(void) {
    return ticks;
}
