%{
#include <stdio.h>
#include <stdlib.h>
int yylex(void);
void yyerror(const char *s);
%}

%union {
    int num;
    char *str;
}

%token <num> NUMBER
%token <str> IDENTIFIER STRING 

%%

chunk:    
        |   chunk stat
        ;

stat:      assignment
        ;

assignment: varlist '=' explist
        ;

varlist:    var
        |   varlist ',' var
        ;

explist:    exp
        |   explist ',' exp
        ;

var:        IDENTIFIER
        ;

exp:        NUMBER
        |   STRING
        ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Error: %s\n", s);
}

int main() {
    yyparse();
}