# This is a Unix Makefile for debugging on Linux.

CC 		= gcc
CFLAGS 	= -lfl

LEX		= flex
YAC 	= bison
YFLAGS	= -d -v

LD		= gcc

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
	$(LD) -o parser $^

# Compile parser object file
$(PARSER_OBJ): $(PARSER_C)
	$(CC) $(CFLAGS) -o $@ -c $< 

# Compile lexer object file  
$(LEXER_OBJ): $(LEXER_C)
	$(CC) $(CFLAGS) -o $@ -c $< 

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
	rm -rf *.exe *.err $(PARSER_C) $(PARSER_H) $(PARSER_OUTPUT) \
	$(LEXER_OBJ) $(PARSER_OBJ) $(LEXER_C)

.PHONY: all clean parser
