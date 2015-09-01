/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton interface for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     Number = 258,
     Name = 259,
     Address = 260,
     Funcs_begin = 261,
     Digraph = 262,
     Lbrace = 263,
     Rbrace = 264,
     Semicolon = 265,
     Ec = 266,
     Sz = 267,
     Equal = 268,
     Lbracket = 269,
     Rbracket = 270,
     Arrow = 271,
     Nodes_begin = 272,
     Edges_begin = 273,
     Comma = 274,
     Ah = 275,
     Am = 276,
     Fm = 277,
     Colon = 278
   };
#endif
/* Tokens.  */
#define Number 258
#define Name 259
#define Address 260
#define Funcs_begin 261
#define Digraph 262
#define Lbrace 263
#define Rbrace 264
#define Semicolon 265
#define Ec 266
#define Sz 267
#define Equal 268
#define Lbracket 269
#define Rbracket 270
#define Arrow 271
#define Nodes_begin 272
#define Edges_begin 273
#define Comma 274
#define Ah 275
#define Am 276
#define Fm 277
#define Colon 278




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 35 "CFG.y"
{
    int iVal;
    char* sVal;
}
/* Line 1529 of yacc.c.  */
#line 100 "y.tab.h"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

extern YYSTYPE yylval;

