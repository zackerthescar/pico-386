CC 		= wcc386
CFLAGS 	= -bt=dos -ecc -3r -fp3

ASM		= nasm
AFLAGS 	= -f coff

LEX		= flex
YAC 	= bison
YFLAGS	= -d -v

LD		= wlink
LFLAGS	= -ecc -bt=dos -bc

# Source files
PARSER_Y = parser.y
LEXER_L = lexer.lex
OUT = parser.exe

# Generated files
PARSER_C = parser.tab.c
PARSER_H = parser.tab.h
PARSER_OUTPUT = parser.output
LEXER_C = lex.yy.c

# Object files
PARSER_OBJ = parser.tab.obj
LEXER_OBJ = lex.yy.obj
OBJS = $(PARSER_OBJ) $(LEXER_OBJ)

all: parser

parser: $(OBJS)
	$(LD) system dos4g $(foreach obj,$^,file $(obj)) name $(OUT)
	cp $(OUT) ../../dos/

# Compile parser object file
$(PARSER_OBJ): $(PARSER_C)
	$(CC) $(CFLAGS) -fo=$@ $< 

# Compile lexer object file  
$(LEXER_OBJ): $(LEXER_C)
	$(CC) $(CFLAGS) -fo=$@ $< 

# Generate parser C file from Bison grammar
$(PARSER_C): $(PARSER_Y)
	$(YAC) $(YFLAGS) -o $@ $<

# Generate lexer C file from Flex specification
$(LEXER_C): $(LEXER_L) $(PARSER_H)
	$(LEX) -o $@ $<

# The lexer depends on parser header for token definitions
$(PARSER_H): $(PARSER_C)

%.obj: %.c
	$(CC) $(CFLAGS) -fo=$@ $< 

%.obj: %.asm
	$(ASM) $(AFLAGS) -o $@ $< 

clean:
	rm -rf *.exe *.err $(PARSER_C) $(PARSER_H) $(PARSER_OUTPUT) $(LEXER_OBJ) $(PARSER_OBJ)

.PHONY: all clean parser
