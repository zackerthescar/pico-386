#include <string.h>
#include "mem.h"

void p8_ram_init(void) {
    uint8_t i;
    memset(&p8_ram, 0, sizeof(p8_ram));
    for (i = 0; i < 16; i++) {
        p8_ram.mem.draw.draw_pal[i] = i;
        p8_ram.mem.draw.screen_pal[i] = i;
    }
    p8_ram.mem.draw.clip_x0 = 0;
    p8_ram.mem.draw.clip_y0 = 0;
    p8_ram.mem.draw.clip_x1 = 128;
    p8_ram.mem.draw.clip_y1 = 128;
    p8_ram.mem.hw.map_width = 128;
    p8_ram.mem.hw.btnp_delay = 15;
    p8_ram.mem.hw.btnp_repeat = 4;
}
