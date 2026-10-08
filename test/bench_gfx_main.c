/*
 * bench_gfx_main.c - instruction counts for one typical frame of drawing.
 *
 * Only for QEMU: run with `-cpu pentium -icount shift=0,sleep=off`. Then
 * the time-stamp counter advances by one per executed guest instruction
 * (each iteration of a REP string instruction counts once), so the
 * differences printed are exact instruction counts. Converting them to
 * 386 time needs a cycles-per-instruction model. `./test.sh bench` runs it.
 *
 * Workload: cls, a full-screen map (16x16 cells, 160 non-zero tiles), 30
 * 8x8 sprites at mixed odd/even positions (every 3rd one flipped), and
 * the VGA blit of the frame.
 */
#include <string.h>
#include "pico386.h"
#include "vga.h"
#include "gfx.h"
#include "mem.h"

P8Ram p8_ram;

extern unsigned rdtsc_lo(void);
#pragma aux rdtsc_lo = ".586" "rdtsc" value [eax] modify [edx];

static void report(const char *name, unsigned instr, int units) {
    debug_serial_printf("BENCH %s %d instr", name, instr);
    if (units > 1) debug_serial_printf(" (%d per unit)", instr / units);
    debug_serial_print("\n");
}

void main(void) {
    unsigned t0, t1, t2, t3, t4;
    int i, tiles = 0;
    unsigned seed = 12345;

    debug_serial_init();
    debug_serial_print("BENCH_START\n");
    p8_ram_init();
    for (i = 0; i < 8192; i++) {            /* sheet: random nibbles */
        seed = seed * 1103515245u + 12345u;
        p8_ram.mem.gfx[i] = (uint8_t)(seed >> 16);
    }
    for (i = 0; i < 256; i++) {             /* map: ~60% non-zero */
        int on = ((i * 7 + (i >> 4) * 3) % 5) < 3;
        tiles += on;
        gfx_mset(i & 15, i >> 4, on ? (uint8_t)(1 + (i % 100)) : 0);
    }
    vga_init();

    t0 = rdtsc_lo();
    gfx_cls(1);
    t1 = rdtsc_lo();
    gfx_map(0, 0, 0, 0, 16, 16, 0);
    t2 = rdtsc_lo();
    for (i = 0; i < 30; i++) {
        gfx_spr(1 + i, (i * 13) & 119, (i * 29) & 119, 8, 8, i % 3 == 0, 0);
    }
    t3 = rdtsc_lo();
    vga_blit_page(p8_ram.mem.screen, 0);
    t4 = rdtsc_lo();
    vga_ret();

    report("cls", t1 - t0, 1);
    report("map", t2 - t1, tiles);
    report("spr30", t3 - t2, 30);
    report("vga_blit", t4 - t3, 1);
    report("total", t4 - t0, 1);
    debug_serial_print("BENCH_DONE\n");
}
