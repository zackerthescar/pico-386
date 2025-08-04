%{

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "parser.tab.h"

int yymylineno = 1;
int last_was_operand = 0; // track unary vs binary

%}

%option noyywrap

%%


"..."           { return VARARGS; }

">>>="          { last_was_operand = 0; return ARS_AS; }
">><="          { last_was_operand = 0; return LRS_AS; }
"<<>="          { last_was_operand = 0; return RLS_AS; }
">>="           { last_was_operand = 0; return ARS_AS; }
"<<="           { last_was_operand = 0; return LLS_AS; }

"^^="           { last_was_operand = 0; return XOR_AS; }
"\\="           { last_was_operand = 0; return NOT_AS; }

"//".*          { /* ignore single line comments */ }

"&="            { last_was_operand = 0; return AND_AS; }
"|="            { last_was_operand = 0; return  OR_AS; }

"+="            { last_was_operand = 0; return PLUS_AS; }
"-="            { last_was_operand = 0; return MINUS_AS; }
"*="            { last_was_operand = 0; return MUL_AS; }
"/="            { last_was_operand = 0; return DIV_AS; }
"^="            { last_was_operand = 0; return POW_AS; }
"%="            { last_was_operand = 0; return MOD_AS; }
"..="           { last_was_operand = 0; return CON_AS; }

"::"            { return DCOLON; }

">><"           { last_was_operand = 0; return LRS; }
">>>"           { last_was_operand = 0; return ARS;}
"<<>"           { last_was_operand = 0; return RLS; }
">>"            { last_was_operand = 0; return ARS; }
"<<"            { last_was_operand = 0; return LLS; }
">="            { last_was_operand = 0; return GEQ; }
"<="            { last_was_operand = 0; return LEQ; }
"=="            { last_was_operand = 0; return EQL; }
"~="            { last_was_operand = 0; return NEQ; }
"!="            { last_was_operand = 0; return NEQ; }
"^^"            { last_was_operand = 0; return XOR; }
".."            { last_was_operand = 0; return CAT; }


"and"           {
    last_was_operand = 0;
    return AND;
}

"or"            {
    last_was_operand = 0; 
    return  OR; 
}

"not"           { 
    last_was_operand = 0;
    return NOT; }


"\\"            { return '\\';}

"~"             { return '~'; }

"%"             { 
    if (last_was_operand) {
        last_was_operand = 0;
        return '%';
    } else {
        return PEEK;
    }
}

"-"             { 
    if (last_was_operand) {
        last_was_operand = 0;
        return '-';
    } else {
        return UMINUS;
    }
}

[=+*/^&|<>#@$;(,:.\[\{] { 
    last_was_operand = 0; 
    return yytext[0]; 
}


[)\]\}]   { 
    last_was_operand = 1;
    return yytext[0];
}


"nil"           { 
    last_was_operand = 1;
    return NIL;  
}

"false"         {
    last_was_operand = 1; 
    return FALSE;
}

"true"          { 
    last_was_operand = 1;
    return TRUE;
}

"return"        { return RETURN; }

"local"         { return LOCAL; }

"break"         { return BREAK; }
"goto"          { return GOTO;  }

"do"            { return DO; }
"while"         { return WHILE; }
"end"           { return END; }
"repeat"        { return REPEAT; }
"until"         { return UNTIL; }
"for"           { return FOR; }
"in"            { return IN; }

"if"            { return IF; }
"then"          { return THEN; }
"elseif"        { return ELIF; }
"else"          { return ELSE; }

"function"      { return FUNCTION; }

[ \t]+          {}
\n              { yymylineno++; }
"--"            {
    /* Handle both single-line and multi-line comments */
    int c = input();
    
    if (c == '[') {
        /* Check if it's a multi-line comment --[[ */
        int next = input();
        if (next == '[') {
            /* Multi-line comment --[[ */
            int bracket_count = 0;
            int in_brackets = 1;
            
            while (in_brackets && (c = input()) != 0) {
                if (c == '\n') {
                    yymylineno++;
                } else if (c == '[') {
                    /* Check for nested [[ */
                    int peek = input();
                    if (peek == '[') {
                        bracket_count++;
                    } else if (peek != 0) {
                        unput(peek);
                    }
                } else if (c == ']') {
                    /* Check for ]] */
                    int peek = input();
                    if (peek == ']') {
                        if (bracket_count == 0) {
                            in_brackets = 0; /* End of comment */
                        } else {
                            bracket_count--;
                        }
                    } else if (peek != 0) {
                        unput(peek);
                    }
                }
            }
        } else {
            /* Not a multi-line comment */
            if (next != 0) unput(next);
            if (c != 0) unput(c);
            
            /* Handle as single-line comment */
            while ((c = input()) != 0 && c != '\n') {
                /* consume rest of line */
            }
            if (c == '\n') {
                unput(c);
            }
        }
    } else {
        /* Single-line comment */
        if (c != 0) unput(c);
        while ((c = input()) != 0 && c != '\n') {
            /* consume rest of line */
        }
        if (c == '\n') {
            unput(c);
        }
    }
}

0b[0-1]+        { 
    last_was_operand = 1;
    yylval.num = strtol(yytext, NULL, 2); 
    return NUMBER; 
}

0x[0-9a-fA-F]+  { 
    last_was_operand = 1;
    yylval.num = strtol(yytext, NULL, 16); 
    return NUMBER; 
}
[0-9]+          { 
    last_was_operand = 1;
    yylval.num = strtol(yytext, NULL, 10); 
    return NUMBER; 
}
[a-zA-Z][a-zA-Z0-9_]*   {
    last_was_operand = 1;
    yylval.str = strdup(yytext); 
    return IDENTIFIER; 
}
_+[a-zA-Z][a-zA-Z0-9_]* {
    last_was_operand = 1;
    yylval.str = strdup(yytext); 
    return IDENTIFIER; 
}

\"[^\"]*\"      { 
    last_was_operand = 1; // screw it
    yylval.str = strdup(yytext); 
    return STRING; 
}

"_"             { return '_';}
.               { return yytext[0]; }

%%