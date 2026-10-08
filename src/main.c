#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "pico386.h"
#include "p386_vm.h"
#include "builtins.h"
#include "p386_builtins.h"
#include "vga.h"
#include "mem.h"
#include "input.h"
#include "kbd.h"
#include "timer.h"

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

/* One game frame passed: time() and the buttons advance. */
static void advance_frame(void) {
    uint8_t down[P8_PLAYERS];
    kbd_read_buttons(down);
    p8_input_frame(down, fps60);
    frame_count++;
    p386_host.time_fp = (int32_t)(((long long)frame_count << 16) / (fps60 ? 60 : 30));
}

/* Show the PICO-8 screen. Returns nonzero when P386_FRAMES is reached. */
static int present(void) {
    uint32_t now = timer_ticks();
    vga_blit(p8_ram.mem.screen);
    vga_apply_screen_pal(p8_ram.mem.draw.screen_pal);
    vga_flip();
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
    p8_input_reset();

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
        while ((int32_t)(timer_ticks() - next_tick) < 0) {
            /* wait for the next frame tick */
        }
        now = timer_ticks();

        updates = 0;
        do {
            if (!run_update(&vm)) return;
            next_tick += frame_step;
        } while ((int32_t)(now - next_tick) >= 0 && ++updates < MAX_CATCHUP);
        if ((int32_t)(now - next_tick) >= 0) next_tick = now + frame_step;  /* drop backlog */

        if (!run_callback(&vm, P386_GLOBAL_DRAW, "_draw")) return;
        if (present()) break;
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
        if (p386_cart_load(&cart, argv[1]) == 0) {
            debug_serial_printf("%s\n", cart.lua_code);

            /* Copy cart sprite/map/flag/sfx/music data into PICO-8 RAM;
             * reload()/cstore() use the same bytes as the cart ROM. */
            if (cart.cart_data) {
                memcpy(p8_ram.raw, cart.cart_data, 0x4300);
                p386_host.cart_rom = cart.cart_data;
                p386_host.cart_rom_size = 0x4300;
            }

            if (p386_cart_compile(&cart) == 0) {
                atexit(shutdown_irqs);
                timer_init();
                kbd_init();
                run_cart(&cart);
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
