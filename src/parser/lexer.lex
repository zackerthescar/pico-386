%{

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "parser.tab.h"

%}

%option noyywrap

%%


[ \t\n]+        {}
"--".*          {}
[0-9]+          { yylval.num = atoi(yytext); return NUMBER; }
[a-zA-Z][a-zA-Z0-9_]*   {yylval.str = strdup(yytext); return IDENTIFIER; }
\"[^\"]*\"      { yylval.str = strdup(yytext); return STRING; }
"="             { return '='; }
.               { return yytext[0]; }

%%
