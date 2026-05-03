#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "pico386.h"
#include "p386_vm.h"
#include "builtins.h"
#include "vga.h"
#include "mem.h"

P8Ram p8_ram;

static void run_callback(P386VMState *vm, uint8_t slot, const char *name) {
    int st;
    if (vm->globals[slot].tag == P386_TAG_NIL) return;
    st = p386_vm_call_global(vm, slot, 0, 0);
    if (st != P386_VM_HALTED) {
        debug_serial_printf("%s failed: %s (%s)\n", name,
                            p386_vm_status_name(st),
                            vm->error_msg ? vm->error_msg : "no message");
    }
}

static void run_cart(P386_Cart *cart) {
    const unsigned char *bc;
    unsigned long bc_len;
    P386VMState vm;
    int st;
    int frame;
    int frames = 3;
    char *env_frames;

    if (!cart || !cart->program) return;
    bc_len = p8_program_bytecode(cart->program, &bc);
    if (!p386_vm_load(&vm, bc, bc_len)) {
        debug_serial_printf("p386_vm_load: %s\n", vm.error_msg ? vm.error_msg : "failed");
        return;
    }

    debug_serial_print("p386_vm_run: main chunk\n");
    st = p386_vm_run(&vm);
    if (st != P386_VM_HALTED) {
        debug_serial_printf("main chunk failed: %s (%s)\n", p386_vm_status_name(st),
                            vm.error_msg ? vm.error_msg : "no message");
        return;
    }

    run_callback(&vm, P386_GLOBAL_INIT, "_init");

    env_frames = getenv("P386_FRAMES");
    if (env_frames && env_frames[0]) frames = atoi(env_frames);
    if (frames < 1) frames = 1;

    for (frame = 0; frame < frames; frame++) {
        if (vm.globals[P386_GLOBAL_UPDATE60].tag != P386_TAG_NIL) {
            run_callback(&vm, P386_GLOBAL_UPDATE60, "_update60");
        } else {
            run_callback(&vm, P386_GLOBAL_UPDATE, "_update");
        }
        run_callback(&vm, P386_GLOBAL_DRAW, "_draw");
        vga_blit(p8_ram.mem.screen);
        vga_flip();
    }
}

void main(int argc, char *argv[]) {
    P386_Cart cart = {0};

    debug_serial_init();
    debug_serial_print("Initializing VGA Mode X 320x400...\n");

    /* Zero PICO-8 RAM and install default draw state. */
    p8_ram_init();

    vga_init();

    if (argc > 1) {
        if (p386_cart_load(&cart, argv[1]) == 0) {
            debug_serial_printf("%s\n", cart.lua_code);

            /* Copy cart sprite/map/flag/sfx/music data into PICO-8 RAM */
            if (cart.cart_data) {
                memcpy(p8_ram.raw, cart.cart_data, 0x4300);
            }

            if (p386_cart_compile(&cart) == 0) {
                run_cart(&cart);
            }
        }
        debug_serial_print("Unloading cart...\n");
        p386_cart_free(&cart);
    }

    sleep(1);
    debug_serial_print("Returning to text mode...\n");
    vga_ret();
}
