%{
#include <stdio.h>
#include <stdlib.h>
int yylex(void);
void yyerror(const char *s);
extern int yymylineno;
%}

%union {
    int num;
    char *str;
}

%token <num> NUMBER
%token <str> IDENTIFIER STRING

%token RETURN
%token NIL
%token FALSE TRUE

%token PLUS_AS MINUS_AS MUL_AS DIV_AS POW_AS MOD_AS
%token NOT_AS XOR_AS AND_AS OR_AS
%token ARS_AS LRS_AS LLS_AS RLS_AS
%token CON_AS
%token PEEK

%token DCOLON BREAK GOTO

%token DO WHILE END REPEAT UNTIL FOR IN

%token IF THEN ELIF ELSE

%token FUNCTION 

%token LOCAL

%token VARARGS

%left OR
%left AND
%left '<' LEQ '>' GEQ EQL NEQ
%left '|'
%left XOR
%left '&'
%left LLS RLS ARS LRS
%left CAT
%left '+' '-'
%left '*' '/' '%' '\\'
%right UMINUS NOT '#' '~' '@' PEEK '$'
%right '^'


%%

chunk:  block;

block:  stat_list
    |   stat_list retstat
    |   retstat
    |   
    ;

stat_list:  stat
    |       stat_list stat
    ;

stat:   ';'
    |   varlist '=' explist
    |   var compop exp
    |   functioncall
    |   '?'
    |   '?' explist
    |   label
    |   BREAK
    |   GOTO IDENTIFIER
    |   DO block END
    |   WHILE exp DO block END
    |   WHILE '(' exp ')' block 
    |   REPEAT block UNTIL exp
    |   IF exp THEN block elif_list else_part END
    |   IF '(' exp ')' block else_part
    |   FOR IDENTIFIER '=' exp ',' exp DO block END
    |   FOR IDENTIFIER '=' exp ',' exp ',' exp DO block END
    |   FOR namelist IN explist DO block END
    |   FUNCTION funcname funcbody
    |   LOCAL FUNCTION IDENTIFIER funcbody
    |   LOCAL namelist
    |   LOCAL namelist '=' explist
    ;

elif_list:  
        |   elif_list ELIF exp THEN block 
        ;

else_part:
        |   ELSE block

retstat:  RETURN  
        | RETURN explist
        | RETURN ';'
        | RETURN explist ';'
        ;

label:  DCOLON IDENTIFIER DCOLON

funcname:   name_chain
        |   name_chain ':' IDENTIFIER
        ;

name_chain:     IDENTIFIER
            |   name_chain '.' IDENTIFIER
            ;

varlist:    var
        |   varlist ',' var
        ;

var:    IDENTIFIER
    |   prefixexp '[' exp ']'
    |   prefixexp '.' IDENTIFIER
    ;

namelist:   IDENTIFIER
        |   namelist ',' IDENTIFIER
        ;

explist:    exp
        |   explist ',' exp
        ;

exp:    NIL
    |   FALSE
    |   TRUE
    |   NUMBER
    |   NUMBER '.' NUMBER // terrible terrible temp hack to get a working parser
	|	'.' NUMBER // another terrible hack
    |   STRING
    |   VARARGS
    |   functiondef
    |   prefixexp
    |   tableconstructor
    |   exp binop exp
    |   unop exp
    |   '(' exp ')'

prefixexp:  var
        |   functioncall
        |   '(' exp ')'
        ;

functioncall:   prefixexp args
            |   prefixexp ':' IDENTIFIER args

args:   '(' ')'
    |   '(' explist ')'
    |   STRING
    ;

functiondef:    FUNCTION funcbody
            ;

funcbody:   '(' ')' block END
        |   '(' parlist ')' block END

parlist:    namelist
        |   VARARGS
        |   namelist ',' VARARGS
        ; 

tableconstructor:   '{' '}'
                |   '{' fieldlist '}'
                ;

fieldlist:  field
        |   field fieldsep
        |   field fieldsep fieldlist
        ;

field:  '[' exp ']' '=' exp
    |   IDENTIFIER '=' exp
    |   exp
    ;  

fieldsep:
        |   ','
        |   ';'
        ;

binop:  '+' | '-' | '*' | '/' | '^' | '%' | '\\'
    |   XOR | '&' | '|' | ARS | LLS | LRS | RLS
    |   CAT | '<' | LEQ | '>' | GEQ | EQL | NEQ
    |   AND | OR
    ;

compop:     PLUS_AS
        |   MINUS_AS
        |   MUL_AS
        |   DIV_AS
        |   POW_AS
        |   MOD_AS
        |   NOT_AS
        |   XOR_AS
        |   AND_AS
        |    OR_AS 
        |   ARS_AS
        |   LRS_AS
        |   LLS_AS
        |   RLS_AS
        |   CON_AS
        ;

unop:   UMINUS
    |   PEEK
    |   NOT
    |   '#'
    |   '~'
    |   '@'
    |   '%'
    |   '$'
    ;
%%

void yyerror(const char *s) {
    fprintf(stderr, "Error: %s at line %d\n", s, yymylineno);
}
