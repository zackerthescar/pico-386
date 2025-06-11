CC 		= wcc386
LD		= wlink
ASM		= nasm
CFLAGS 	= -i=include -i=zlib -bt=dos -ecc -3r -fp3 -d2 -od
AFLAGS 	= -f obj -g
LFLAGS	= -ecc -bt=dos -bc

OBJS	= src/main.obj src/print.obj src/vga.obj src/cart.obj
C_SRC	= src/main.c src/print.c src/vga.c src/cart.c
OUT		= dos/MAIN.EXE

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

all: pico

pico: $(OBJS) $(ZLIB_LIB)
	$(LD) system dos4g debug all $(foreach obj,$^,file $(obj)) name $(OUT)

$(ZLIB_LIB): $(ZLIB_OBJS)
	wlib -b -c $(ZLIB_LIB) $(foreach obj,$(ZLIB_OBJS),-+$(obj))

src/%.obj: src/%.c
	$(CC) $(CFLAGS) -fo=$@ $< 

src/%.obj: src/%.asm
	$(ASM) $(AFLAGS) -o $@ $< 

$(ZLIB_DIR)/%.obj: $(ZLIB_DIR)/%.c
	$(CC) -bt=dos -3r -fp3 -ecc -od -fo=$@ $< 

clean:
	rm -rf $(OBJS) $(ZLIB_OBJS) $(ZLIB_LIB) dos/*.exe *.err

.PHONY: all clean pico
