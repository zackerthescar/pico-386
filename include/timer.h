#ifndef _TIMER_H
#define _TIMER_H

#include <stdint.h>

/*
 * PIT channel 0 reprogrammed to TIMER_HZ. The INT 8 handler counts ticks
 * and still calls the BIOS handler at ~18.2 Hz, so the DOS clock and the
 * floppy motor timeout keep working.
 *
 * 60 Hz: _update60 runs every tick, _update every second tick.
 */

#define TIMER_HZ 60

void timer_init(void);
void timer_shutdown(void);

/* Ticks since timer_init(). Wraps after ~2 years at 60 Hz; compare with
 * signed differences. */
uint32_t timer_ticks(void);

#endif
