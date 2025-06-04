CC 		= wcc386
LD		= wlink
ASM		= nasm
CFLAGS 	= -i=include -bt=dos -ecc -3r -fp3 -d2 -od
AFLAGS 	= -f obj -g
LFLAGS	= -ecc -bt=dos -bc

OBJS	= src/main.obj src/print.obj src/vga.obj src/cart.obj
C_SRC	= src/main.c src/print.c src/vga.c src/cart.c
OUT		= dos/MAIN.EXE

all: pico

pico: $(OBJS)
	$(LD) system dos4g debug all $(foreach obj,$^,file $(obj)) name $(OUT) 

%.obj: %.c
	$(CC) $(CFLAGS) -fo=$@ $< 

%.obj: %.asm
	$(ASM) $(AFLAGS) -o $@ $< 

clean:
	rm -rf $(OBJS) dos/*.exe *.err
