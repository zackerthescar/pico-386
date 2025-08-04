CC 		= wcc386
CFLAGS 	= -i=include -i=zlib -bt=dos -ecc -3r -fp3


ASM		= nasm
AFLAGS 	= -f coff

LEX		= flex
YACC 	= bison

LD		= wlink
LFLAGS	= -ecc -bt=dos -bc

PARSER_Y	=	src/parser/parser.y
LEXER_L		=	src/parser/lexer.lex

C_SRC	= 	src/main.c 					\
			src/print.c					\
			src/vga.c					\
			src/cart.c 					\
			src/pxa_compress_snippets.c	\
			src/p8_compress.c

ASM_SRC	=	src/serial.asm

PARSER_C	= 	src/parser.tab.c
PARSER_H	= 	include/parser.tab.h
PARSER_OUT	= 	src/parser/parser.output
LEXER_C		=	src/lex.yy.c
LEXER_H		=	include/lex.yy.h

PARSER_OBJ 	=	src/parser.tab.obj
LEXER_OBJ  	=	src/lex.yy.obj

OBJS	= $(C_SRC:.c=.obj) $(ASM_SRC:.asm=.obj)
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

pico: $(ZLIB_LIB) $(PARSER_OBJ) $(LEXER_OBJ) $(OBJS)
	$(LD) system dos4g $(foreach obj,$^,file $(obj)) name $(OUT)

$(ZLIB_LIB): $(ZLIB_OBJS)
	wlib -b -c $(ZLIB_LIB) $(foreach obj,$(ZLIB_OBJS),-+$(obj))

$(PARSER_OBJ): $(PARSER_C)
	$(CC) $(CFLAGS) -fo=$@ $<

$(LEXER_OBJ): $(LEXER_C)
	$(CC) $(CFLAGS) -fo=$@ $<

$(PARSER_C): $(PARSER_Y)
	$(YACC) $(YFLAGS) -o $@ --header=$(PARSER_H) $<

$(LEXER_C): $(LEXER_L) $(PARSER_H)
	$(LEX) -o $@ --header-file=$(LEXER_H) $<

$(PARSER_H): $(PARSER_C)

$(LEXER_H): $(LEXER_C)

src/%.obj: src/%.c $(LEXER_H)
	$(CC) $(CFLAGS) -fo=$@ $< 

src/%.obj: src/%.asm
	$(ASM) $(AFLAGS) -o $@ $< 

$(ZLIB_DIR)/%.obj: $(ZLIB_DIR)/%.c
	$(CC) -bt=dos -3r -fp3 -ecc -os -fo=$@ $< 

clean:
	rm -rf $(OBJS) $(ZLIB_OBJS) $(ZLIB_LIB) dos/MAIN.exe *.err \
		$(PARSER_C) $(PARSER_H) $(PARSER_OUT) $(LEXER_C) \
		$(PARSER_OBJ) $(LEXER_OBJ) $(LEXER_H)


.PHONY: all clean pico
