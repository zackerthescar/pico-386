#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "pico386.h"
#include "p386_vm.h"
#include "builtins.h"
#include "p386_builtins.h"
#include "vga.h"
#include "mem.h"
#include "input.h"
#include "kbd.h"
#include "timer.h"
#include "p386_gc.h"

P8Ram p8_ram;

/* Max _update calls per _draw when behind. Past this the game slows down
 * instead of spending every frame catching up. */
#define MAX_CATCHUP 4

/* Frame pacing shared by the main loop and flip(). */
static int fps60;                       /* cart uses _update60 */
static uint32_t frame_step;             /* timer ticks per frame */
static uint32_t next_tick;              /* tick the next frame is due */
static uint32_t frame_count;            /* updates (or flips) so far */
static long frame_limit;                /* P386_FRAMES; 0 = run until Esc */
static long drawn;                      /* frames shown */
static uint32_t fps_tick, fps_drawn;    /* stat(7) measurement */

static void set_frame_rate(int is60) {
    fps60 = is60;
    frame_step = is60 ? TIMER_HZ / 60 : TIMER_HZ / 30;
    p386_host.target_fps = is60 ? 60 : 30;
}

/* Test harness: scripted buttons for player 0. P386_KEYS is a list of
 * "first-last:mask" items (frames counted from 1, mask = btn() bits), e.g.
 * "20-24:16,60-200:2" presses O on frames 20-24 and holds right on 60-200. */
static const char *key_script;

static uint8_t scripted_buttons(uint32_t frame) {
    const char *p = key_script;
    uint8_t mask = 0;
    while (p && *p) {
        char *end;
        unsigned long first = strtoul(p, &end, 10), last = first, m;
        if (*end == '-') last = strtoul(end + 1, &end, 10);
        if (*end != ':') break;
        m = strtoul(end + 1, &end, 0);
        if (frame >= first && frame <= last) mask |= (uint8_t)m;
        p = (*end == ',') ? end + 1 : end;
    }
    return mask;
}

#ifdef P386_PROF
/* Profiling build (PROF.EXE). Only for QEMU with -icount: the time-stamp
 * counter then counts guest instructions. For each shown frame it measures
 * _update (with catch-up calls), _draw, the C builtins they call (timed by
 * the dispatcher), the collector, and the blit. P386_PROF=n skips the
 * first n frames (cart start-up). */
extern unsigned prof_tsc(void);
#pragma aux prof_tsc = ".586" "rdtsc" value [eax] modify [edx];

unsigned long long p386_prof_cfunc;     /* added to by p386_dispatch.asm: */
unsigned long long p386_prof_table;     /*   builtins, table and string    */
unsigned long long p386_prof_string;    /*   helpers called by the VM      */
unsigned p386_prof_ops;                 /* bytecodes run (NEXT)            */
unsigned p386_prof_op_cost[256];        /* PROFOPS.EXE: per opcode          */
unsigned p386_prof_op_count[256];
static unsigned long long prof_gc;
static unsigned prof_gc_t0, prof_blit;
static long prof_skip;

#define PROF_MAX_FRAMES 512
typedef struct { const char *name; unsigned long sum, max; unsigned v[PROF_MAX_FRAMES]; } ProfStat;
enum { PS_UPDATE, PS_DRAW, PS_BUILTINS, PS_GC, PS_INTERP, PS_TABLE, PS_STRING, PS_DISPATCH,
       PS_OPS, PS_BLIT, PS_FRAME, PS_COUNT };
static ProfStat prof_stats[PS_COUNT] = {
    { "update" }, { "draw" }, { "builtins" }, { "gc" }, { "interp" },
    { "table" }, { "string" }, { "dispatch" }, { "bytecodes" }, { "blit" }, { "frame" }
};
static unsigned long prof_frames, prof_updates;

static void prof_gc_hook(int end) {
    unsigned t = prof_tsc();
    if (!end) prof_gc_t0 = t;
    else prof_gc += t - prof_gc_t0;
}

static void prof_add(int i, unsigned v) {
    prof_stats[i].sum += v;
    if (v > prof_stats[i].max) prof_stats[i].max = v;
    if (prof_frames <= PROF_MAX_FRAMES) prof_stats[i].v[prof_frames - 1] = v;
}

static int cmp_unsigned(const void *a, const void *b) {
    unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;
    return x < y ? -1 : x > y;
}

static void prof_report(void) {
    int i;
    if (prof_frames == 0) return;
    /* debug_serial_printf knows %d, not %lu; the values fit in an int. */
    debug_serial_printf("PROF frames %d updates %d\n", (int)prof_frames, (int)prof_updates);
    for (i = 0; i < PS_COUNT; i++) {
        ProfStat *st = &prof_stats[i];
        unsigned n = prof_frames < PROF_MAX_FRAMES ? (unsigned)prof_frames : PROF_MAX_FRAMES;
        qsort(st->v, n, sizeof(st->v[0]), cmp_unsigned);
        debug_serial_printf("PROF %s median %d p90 %d max %d avg %d\n", st->name,
                            (int)st->v[n / 2], (int)st->v[n * 9 / 10], (int)st->max,
                            (int)(st->sum / prof_frames));
    }
    for (i = 0; i < 256; i++) {
        if (p386_prof_op_count[i]) {
            debug_serial_printf("PROFOP %x count %d cost %d\n", i,
                                (int)p386_prof_op_count[i], (int)p386_prof_op_cost[i]);
        }
    }
    debug_serial_print("PROF_DONE\n");
}
#endif

/* One game frame passed: time() and the buttons advance. */
static void advance_frame(void) {
    uint8_t down[P8_PLAYERS];
    kbd_read_buttons(down);
    if (key_script) down[0] |= scripted_buttons(frame_count + 1);
    p8_input_frame(down, fps60);
    frame_count++;
    p386_host.time_fp = (int32_t)(((long long)frame_count << 16) / (fps60 ? 60 : 30));
}

/* cartdata(): the save slot (0x5E00-0x5EFF) lives in a file named after a
 * hash of the id, because DOS names have 8.3 characters. */
#define CARTDATA_ADDR 0x5E00u
#define CARTDATA_SIZE 256u
static char cartdata_file[16];
static uint8_t cartdata_saved[CARTDATA_SIZE];

static int host_cartdata_open(const char *id, uint8_t *data) {
    uint32_t h = 2166136261u;
    FILE *f;
    int loaded = 0;
    for (; *id; id++) h = (h ^ (uint8_t)*id) * 16777619u;
    sprintf(cartdata_file, "%08lX.P8D", (unsigned long)h);
    f = fopen(cartdata_file, "rb");
    if (f) {
        loaded = fread(data, 1, CARTDATA_SIZE, f) == CARTDATA_SIZE;
        fclose(f);
    }
    if (!loaded) memset(data, 0, CARTDATA_SIZE);
    memcpy(cartdata_saved, data, CARTDATA_SIZE);
    return loaded;
}

/* Write the save slot when dset() or poke() changed it. */
static void cartdata_flush(void) {
    const uint8_t *data = p8_ram.raw + CARTDATA_ADDR;
    FILE *f;
    if (!cartdata_file[0] || memcmp(data, cartdata_saved, CARTDATA_SIZE) == 0) return;
    f = fopen(cartdata_file, "wb");
    if (!f) return;
    if (fwrite(data, 1, CARTDATA_SIZE, f) == CARTDATA_SIZE) {
        memcpy(cartdata_saved, data, CARTDATA_SIZE);
    }
    fclose(f);
}

/* Show the PICO-8 screen. Returns nonzero when P386_FRAMES is reached. */
static int present(void) {
    uint32_t now = timer_ticks();
#ifdef P386_PROF
    unsigned t0 = prof_tsc();
#endif
    vga_blit(p8_ram.mem.screen);
    vga_apply_screen_pal(p8_ram.mem.draw.screen_pal);
#ifdef P386_PROF
    prof_blit = prof_tsc() - t0;
#endif
    vga_flip();
    cartdata_flush();
    fps_drawn++;
    if ((int32_t)(now - fps_tick) >= TIMER_HZ) {
        p386_host.fps = (int32_t)fps_drawn;
        fps_drawn = 0;
        fps_tick = now;
    }
    return frame_limit > 0 && ++drawn >= frame_limit;
}

static void wait_next_frame(void) {
    uint32_t now;
    while ((int32_t)(timer_ticks() - next_tick) < 0) {
        /* wait for the next frame tick */
    }
    now = timer_ticks();
    next_tick += frame_step;
    if ((int32_t)(now - next_tick) >= 0) next_tick = now + frame_step;
}

/* flip() for carts with their own loop. Nonzero stops the cart. */
static int host_flip(void) {
    int stop = present();
    wait_next_frame();
    advance_frame();
    return stop || kbd_take_hit(KBD_ESC);
}

static int run_callback(P386VMState *vm, uint8_t slot, const char *name) {
    int st;
    if (vm->globals[slot].tag == P386_TAG_NIL) return 1;
    st = p386_vm_call_global(vm, slot, 0, 0);
    if (st == P386_VM_ERR_QUIT) return 0;
    if (st != P386_VM_HALTED) {
        debug_serial_printf("%s failed: %s (%s)\n", name,
                            p386_vm_status_name(st),
                            vm->error_msg ? vm->error_msg : "no message");
        return 0;
    }
    return 1;
}

static int run_update(P386VMState *vm) {
    advance_frame();
    if (fps60) return run_callback(vm, P386_GLOBAL_UPDATE60, "_update60");
    return run_callback(vm, P386_GLOBAL_UPDATE, "_update");
}

static void run_cart(P386_Cart *cart) {
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;
    int st;
    uint32_t now;
    int updates;
    char *env_frames;

    if (!cart || !cart->program) return;
    bc_len = p8_program_bytecode(cart->program, &bc);
    if (!p386_vm_load(&vm, bc, bc_len)) {
        debug_serial_printf("p386_vm_load: %s\n", vm.error_msg ? vm.error_msg : "failed");
        return;
    }

    /* Test harness: stop after N shown frames. */
    env_frames = getenv("P386_FRAMES");
    frame_limit = (env_frames && env_frames[0]) ? atol(env_frames) : 0;

    /* The main chunk may run its own loop with flip() at 30 fps. */
    set_frame_rate(0);
    next_tick = fps_tick = timer_ticks();
    p386_host.flip = host_flip;
    p386_host.cartdata_open = host_cartdata_open;
    p8_input_reset();
    key_script = getenv("P386_KEYS");
#ifdef P386_PROF
    {
        char *env_prof = getenv("P386_PROF");
        prof_skip = (env_prof && env_prof[0]) ? atol(env_prof) : 0;
        p386_gc_hook = prof_gc_hook;
    }
#endif

    debug_serial_print("p386_vm_run: main chunk\n");
    st = p386_vm_run(&vm);
    if (st == P386_VM_ERR_QUIT) return;
    if (st != P386_VM_HALTED) {
        debug_serial_printf("main chunk failed: %s (%s)\n", p386_vm_status_name(st),
                            vm.error_msg ? vm.error_msg : "no message");
        return;
    }

    if (!run_callback(&vm, P386_GLOBAL_INIT, "_init")) return;

    /* The PIT is the game clock; vga_flip only stops tearing. On the 70 Hz
     * display a 60 fps frame sometimes shows for two refreshes, but game
     * speed stays exact. */
    set_frame_rate(vm.globals[P386_GLOBAL_UPDATE60].tag != P386_TAG_NIL);
    next_tick = timer_ticks();

    while (!kbd_take_hit(KBD_ESC)) {
#ifdef P386_PROF
        unsigned t0, t1, t2, cf0, cf1, cf2, gc0, gc2, tb0, st0, op0;
#endif
        while ((int32_t)(timer_ticks() - next_tick) < 0) {
            /* wait for the next frame tick */
        }
        now = timer_ticks();

#ifdef P386_PROF
        cf0 = (unsigned)p386_prof_cfunc;
        gc0 = (unsigned)prof_gc;
        tb0 = (unsigned)p386_prof_table;
        op0 = p386_prof_ops;
        st0 = (unsigned)p386_prof_string;
        t0 = prof_tsc();
#endif
        updates = 0;
        do {
            if (!run_update(&vm)) return;
            next_tick += frame_step;
        } while ((int32_t)(now - next_tick) >= 0 && ++updates < MAX_CATCHUP);
        if ((int32_t)(now - next_tick) >= 0) next_tick = now + frame_step;  /* drop backlog */

#ifdef P386_PROF
        t1 = prof_tsc();
        cf1 = (unsigned)p386_prof_cfunc;
#endif
        if (!run_callback(&vm, P386_GLOBAL_DRAW, "_draw")) return;
#ifdef P386_PROF
        t2 = prof_tsc();
        cf2 = (unsigned)p386_prof_cfunc;
        gc2 = (unsigned)prof_gc;
#endif
        if (present()) {
#ifdef P386_PROF
            prof_report();
#endif
            break;
        }
#ifdef P386_PROF
        if (drawn > prof_skip) {
            unsigned lua = t2 - t0, builtins = cf2 - cf0, gc = gc2 - gc0;
            unsigned table = (unsigned)p386_prof_table - tb0;
            unsigned string = (unsigned)p386_prof_string - st0;
            (void)cf1;
            prof_frames++;
            prof_updates += (unsigned long)updates + 1;
            prof_add(PS_UPDATE, t1 - t0);
            prof_add(PS_DRAW, t2 - t1);
            prof_add(PS_BUILTINS, builtins);
            prof_add(PS_GC, gc);
            prof_add(PS_INTERP, lua - builtins - gc);
            prof_add(PS_TABLE, table);
            prof_add(PS_STRING, string);
            /* The counter itself costs one instruction per bytecode. */
            prof_add(PS_OPS, p386_prof_ops - op0);
            prof_add(PS_DISPATCH, lua - builtins - gc - table - string - (p386_prof_ops - op0));
            prof_add(PS_BLIT, prof_blit);
            prof_add(PS_FRAME, lua + prof_blit);
        }
#endif
    }
}

static void shutdown_irqs(void) {
    kbd_shutdown();
    timer_shutdown();
}

void main(int argc, char *argv[]) {
    P386_Cart cart = {0};

    debug_serial_init();
    debug_serial_print("Initializing VGA Mode X, 3 scanlines per row...\n");

    /* Zero PICO-8 RAM and install default draw state. */
    p8_ram_init();

    vga_init();

    if (argc > 1) {
#ifdef P386_PROF
        unsigned tl0, tl1, tl2;
        volatile unsigned long *bios_ticks = (volatile unsigned long *)0x46C;
        unsigned long k0 = *bios_ticks, k1 = 0, k2;
        tl0 = getenv("P386_NOTSC") ? 0 : prof_tsc();
#endif
        if (p386_cart_load(&cart, argv[1]) == 0) {
            /* Do not dump the source: at 115200 baud, 26 KB is 2.3 s. */
            debug_serial_printf("Lua source: %d bytes\n", (int)strlen(cart.lua_code));

            /* Copy cart sprite/map/flag/sfx/music data into PICO-8 RAM;
             * reload()/cstore() use the same bytes as the cart ROM. */
            if (cart.cart_data) {
                memcpy(p8_ram.raw, cart.cart_data, 0x4300);
                p386_host.cart_rom = cart.cart_data;
                p386_host.cart_rom_size = 0x4300;
            }

#ifdef P386_PROF
            tl1 = getenv("P386_NOTSC") ? 0 : prof_tsc();
            k1 = *bios_ticks;
#endif
            if (p386_cart_compile(&cart) == 0) {
#ifdef P386_PROF
                tl2 = getenv("P386_NOTSC") ? 0 : prof_tsc();
                k2 = *bios_ticks;
                printf("PROFTICKS load=%lu compile=%lu (18.2/s)\n", k1 - k0, k2 - k1); fflush(stdout);
                debug_serial_printf("PROFLOAD load=%d compile=%d\n",
                    (int)(tl1 - tl0), (int)(tl2 - tl1));
                printf("PROFLOAD load=%d compile=%d\n",
                    (int)(tl1 - tl0), (int)(tl2 - tl1));
#endif
                atexit(shutdown_irqs);
                timer_init();
                kbd_init();
                run_cart(&cart);
                cartdata_flush();
                /* Test harness: keep the last frame on screen for a
                 * screendump; the harness quits QEMU. */
                if (getenv("P386_HOLD")) {
                    debug_serial_print("P386_HOLD: frame on screen\n");
                    for (;;) {}
                }
                shutdown_irqs();
            }
        }
        debug_serial_print("Unloading cart...\n");
        p386_cart_free(&cart);
    }

    sleep(1);
    debug_serial_print("Returning to text mode...\n");
    vga_ret();
}
