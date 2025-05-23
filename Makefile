CC 		= wcc
LD		= wcl
CFLAGS 	= -2 -fp2
LFLAGS	= 

OBJS	= src/main.o src/serial.o src/vga.o
SOURCE	= src/main.c src/serial.c src/vga.c
OUT		= dos/main.exe

all: pico

pico: $(OBJS)
	$(LD) -fe=$(OUT) $^ $(LFLAGS)

%.o: %.c
	$(CC) -fo=$@ -i=include $< $(CFLAGS) 

clean:
	rm -rf $(OBJS) dos/*
