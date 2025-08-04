#include "parser.tab.h"

extern int yydebug;

int main(int argc, char *argv[]) {
        yydebug=1;
        yyparse();
        return 0;
}
