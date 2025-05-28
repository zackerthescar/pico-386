CC 		= wcc386
LD		= wcl386
ASM		= nasm
CFLAGS 	= -i=include -ecc -3 -fp3
AFLAGS 	= -f obj
LFLAGS	= -ecc -bt=dos -bc

OBJS	= src/main.o src/serial.o src/print.o src/vga.o
C_SRC	= src/main.c src/print.c src/vga.c
ASM_SRC	= src/serial.asm
OUT		= dos/main.exe

all: pico

pico: $(OBJS)
	$(LD) $(LFLAGS) -fe=$(OUT) $^ 

%.o: %.c
	$(CC) $(CFLAGS) -fo=$@ $< 

%.o: %.asm
	$(ASM) $(AFLAGS) -o $@ $< 

clean:
	rm -rf $(OBJS) dos/* *.err
