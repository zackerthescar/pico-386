CC 		= wcc386
LD		= wlink
ASM		= nasm
CFLAGS 	= -i=include -i=zlib -bt=dos -ecc -3s
AFLAGS 	= -f elf32
LFLAGS	= -ecc -bt=dos -bc

RUST_TARGET	= i386-dos4gw.json
RUST_OUT	= rust/target/i386-dos4gw/release
RUST_AR		= $(RUST_OUT)/libpico386_rs.a
RUST_OBJS_DIR = rust/objs

C_SRC	= 	src/main.c 					\
			src/pico386.c				\
			src/print.c					\
			src/cart.c 					\
			src/pxa_compress_snippets.c	\
			src/p8_compress.c

ASM_SRC	=	src/serial.asm src/vga.asm src/p386_dispatch.asm

# CRT shim bridges Rust ELF references to Watcom C runtime
CRT_SHIM = src/crt_shim.obj

OBJS	= $(C_SRC:.c=.obj) $(ASM_SRC:.asm=.obj)
OUT		= dos/MAIN.EXE

# Shared objects (everything except main.obj — used by both MAIN.EXE and TEST.EXE)
LIB_OBJS = 	src/pico386.obj				\
			src/p386_loader.obj			\
			src/p386_obj.obj			\
			src/p386_dispatch.obj		\
			src/print.obj				\
			src/vga.obj					\
			src/cart.obj				\
			src/pxa_compress_snippets.obj	\
			src/p8_compress.obj			\
			src/serial.obj

# Test sources
TEST_SRC	= test/test_main.c
TEST_OBJ	= test/test_main.obj
TEST_OUT	= dos/TEST.EXE
VM_TEST_SRC	= test/test_vm_main.c
VM_TEST_OBJ	= test/test_vm_main.obj
VM_TEST_OUT	= dos/VMTEST.EXE

# VGA test
VGA_TEST_OBJ	= test/test_vga_main.obj
VGA_TEST_OUT	= dos/VGATEST.EXE

# zlib stuff
ZLIB_DIR = zlib
ZLIB_LIB = $(ZLIB_DIR)/zlib.lib

ZLIB_SOURCES = 	$(ZLIB_DIR)/adler32.c	\
				$(ZLIB_DIR)/compress.c	\
				$(ZLIB_DIR)/crc32.c		\
				$(ZLIB_DIR)/deflate.c	\
				$(ZLIB_DIR)/gzclose.c	\
				$(ZLIB_DIR)/gzlib.c		\
				$(ZLIB_DIR)/gzread.c	\
				$(ZLIB_DIR)/gzwrite.c	\
               	$(ZLIB_DIR)/infback.c  	\
				$(ZLIB_DIR)/inffast.c  	\
				$(ZLIB_DIR)/inflate.c 	\
				$(ZLIB_DIR)/inftrees.c 	\
               	$(ZLIB_DIR)/trees.c		\
				$(ZLIB_DIR)/uncompr.c  	\
				$(ZLIB_DIR)/zutil.c

ZLIB_OBJS = $(ZLIB_SOURCES:.c=.obj)
ZLIB_VERSION = 1.2.11

RUST_OBJS = $(wildcard $(RUST_OBJS_DIR)/*.o)

all: pico

dos:
	mkdir -p dos

$(ZLIB_DIR)/zconf.h:
	mkdir -p $(ZLIB_DIR)
	curl -sL https://zlib.net/fossils/zlib-$(ZLIB_VERSION).tar.gz | tar xz --strip-components=1 -C $(ZLIB_DIR)

wstub.exe:
	cp $(WATCOM)/binw/wstub.exe .

$(RUST_AR): FORCE
	cd rust && cargo build -Zbuild-std=core,alloc -Zjson-target-spec --target=../$(RUST_TARGET) --release

$(RUST_OBJS_DIR): $(RUST_AR)
	rm -rf $(RUST_OBJS_DIR) && mkdir -p $(RUST_OBJS_DIR)
	cd $(RUST_OBJS_DIR) && ar x ../../$(RUST_AR) $$(ar t ../../$(RUST_AR) | grep pico386_rs)
	@echo "Patching ELF R_386_PLT32 -> R_386_PC32 for wlink compatibility..."
	@python3 -c "$$ELF_PATCH_SCRIPT" $(RUST_OBJS_DIR)/*.o

define ELF_PATCH_SCRIPT
import struct, sys
for path in sys.argv[1:]:
    with open(path, 'rb') as f: data = bytearray(f.read())
    shoff = struct.unpack_from('<I', data, 32)[0]
    shsz = struct.unpack_from('<H', data, 46)[0]
    shnum = struct.unpack_from('<H', data, 48)[0]
    n = 0
    for i in range(shnum):
        o = shoff + i * shsz
        if struct.unpack_from('<I', data, o+4)[0] != 9: continue
        roff = struct.unpack_from('<I', data, o+16)[0]
        rsz = struct.unpack_from('<I', data, o+20)[0]
        for j in range(0, rsz, 8):
            info = struct.unpack_from('<I', data, roff+j+4)[0]
            if info & 0xff == 4:
                struct.pack_into('<I', data, roff+j+4, (info & ~0xff) | 2)
                n += 1
    with open(path, 'wb') as f: f.write(data)
    if n: print(f'  Patched {n} relocations in {path}')
endef
export ELF_PATCH_SCRIPT

src/crt_shim.obj: src/crt_shim.c
	$(CC) $(CFLAGS) -fo=$@ $<

pico: wstub.exe dos $(OBJS) $(ZLIB_LIB) $(RUST_OBJS_DIR) $(CRT_SHIM)
	$(LD) system dos4g $(foreach obj,$(OBJS),file $(obj)) file $(CRT_SHIM) $(foreach obj,$(wildcard $(RUST_OBJS_DIR)/*.o),file $(obj)) library $(ZLIB_LIB) name $(OUT)

# Test binary — links test runner + shared libs (no main.obj, no vga.obj needed for tests but included for unload() etc.)
test: wstub.exe dos $(TEST_OBJ) $(LIB_OBJS) $(ZLIB_LIB) $(RUST_OBJS_DIR) $(CRT_SHIM)
	$(LD) system dos4g file $(TEST_OBJ) $(foreach obj,$(LIB_OBJS),file $(obj)) file $(CRT_SHIM) $(foreach obj,$(wildcard $(RUST_OBJS_DIR)/*.o),file $(obj)) library $(ZLIB_LIB) name $(TEST_OUT)

vm-test: wstub.exe dos $(VM_TEST_OBJ) src/p386_loader.obj src/p386_obj.obj src/p386_dispatch.obj src/serial.obj src/print.obj
	$(LD) system dos4g file $(VM_TEST_OBJ) file src/p386_loader.obj file src/p386_obj.obj file src/p386_dispatch.obj file src/serial.obj file src/print.obj name $(VM_TEST_OUT)

# VGA test binary — needs VGA + serial, minimal deps
vga-test: wstub.exe dos $(VGA_TEST_OBJ) src/vga.obj src/serial.obj src/print.obj
	$(LD) system dos4g file $(VGA_TEST_OBJ) file src/vga.obj file src/serial.obj file src/print.obj name $(VGA_TEST_OUT)

$(ZLIB_LIB): $(ZLIB_DIR)/zconf.h
	$(MAKE) $(ZLIB_OBJS)
	wlib -b -c $(ZLIB_LIB) $(foreach obj,$(ZLIB_OBJS),-+$(obj))

src/%.obj: src/%.c $(ZLIB_DIR)/zconf.h
	$(CC) $(CFLAGS) -fo=$@ $<

# asm builds as ELF32 — wlink consumes it alongside OMF C objects.
src/%.obj: src/%.asm
	$(ASM) $(AFLAGS) -o $@ $<

# Test main includes other test .c files directly, so depend on all of them
TEST_DEPS = $(wildcard test/*.c)
test/%.obj: test/%.c $(TEST_DEPS)
	$(CC) $(CFLAGS) -i=test -fo=$@ $<

$(ZLIB_DIR)/%.obj: $(ZLIB_DIR)/%.c $(ZLIB_DIR)/zconf.h
	$(CC) -bt=dos -3s -ecc -od -fo=$@ $<

clean:
	rm -rf $(OBJS) $(ZLIB_OBJS) $(ZLIB_LIB) $(RUST_OBJS_DIR) $(CRT_SHIM) $(TEST_OBJ) $(VM_TEST_OBJ) $(VGA_TEST_OBJ) dos/*.exe *.err
	cd rust && cargo clean

FORCE:
.PHONY: all clean pico test vm-test vga-test FORCE
