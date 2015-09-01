%{
    #include <stdio.h>
    #include "y.tab.h"
    void yyerror(char* );
%}

%%

"digraph"                   return Digraph;
"ADDR"                      return Address;
"EC"                        return Ec;
"SZ"                        return Sz;
"AH"                        return Ah;
"AM"                        return Am;
"FM"                        return Fm;
"FUNCTIONS"                 return Funcs_begin;
"NODES"                     return Nodes_begin;
"EDGES"                     return Edges_begin;
[a-zA-Z][a-zA-Z0-9\-_]*     yylval.sVal=strdup(yytext); return Name;
[0-9]+                      yylval.iVal=atoi(yytext); return Number;
\{                          return Lbrace;
\}                          return Rbrace;
;                           return Semicolon;
:                           return Colon;
,                           return Comma;
"["                         return Lbracket;
"]"                         return Rbracket;
=                           return Equal;
"->"                        return Arrow;
[ \t\n]+                    /* ignore whitespaces */
.                           yyerror("unknown token");

%%
