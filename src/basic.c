/*
 *  Basic Parser - TurboBasic XL compatible parsing and transformation tool.
 *  Copyright (C) 2015 Daniel Serpell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#include "parser-peg.h"
#include "statements.h"
#include "tokens.h"
#include "vars.h"
#include "dbg.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

//#define YY_DEBUG

// Our input file
FILE *inFile = 0;

// Define YY_INPUT to parse from file
#define YY_INPUT(buf, result, max_size)                 \
  {                                                     \
    int yyc= getc(inFile);                              \
    result= (EOF == yyc) ? 0 : (*(buf)= yyc, 1);        \
  }

// YY_PARSE function is local to this file
#define YY_PARSE(T) static T

// Rules that return values return an "expr *"
typedef struct expr_struct expr;
#define YYSTYPE expr *

// Checks the input searching for a statement
static int testStatement(int turbo_stmt, enum enum_statements e);
static int testToken(int turbo_tok, enum enum_tokens e);
static int parsingTurbo(void);

#include "basic_peg.c"

// Match a character ignoring case
static int matchIgnoreCase(yycontext *yy, int c)
{
  if (yy->__pos >= yy->__limit && !yyrefill(yy)) return 0;

  int b = (unsigned char)yy->__buf[yy->__pos];

  // Transform both to upper-case
  c = (c>='a' && c<='z') ? c+'A'-'a' : c;
  b = (b>='a' && b<='z') ? b+'A'-'a' : b;

  if( b == c )
  {
      ++yy->__pos;
      return 1;
  }
  return 0;
}

// Checks if we are parsing TurboBasicXL
static int parsingTurbo(void)
{
    return (parser_get_dialect() == parser_dialect_turbo);
}

// Checks the input searching for a statement
static int testStatement(int turbo_stmt, enum enum_statements e)
{
    const struct statements *s = &statements[e];
    int i;
    yycontext *yy = yyctx;
    int yypos0= yy->__pos, yythunkpos0= yy->__thunkpos;
    enum parser_mode mode = parser_get_mode();

    // Skip if dialect is not turbo and statement is
    if( turbo_stmt && !parsingTurbo() )
        return 0;

    // Special case PRINT:
    if( e == STMT_PRINT_ )
        return !!yymatchChar(yy, '?');

    // Check each character
    for(i=0; 0 != s->stm_long[i]; i++)
    {
        // If enough characters are already tested and input is a dot, accept
        if( i>=s->min && yymatchChar(yy, '.') )
            return 1;
        // Else, if not the correct character, return no match
        if( !matchIgnoreCase(yy, s->stm_long[i]) )
        {
            yy->__pos = yypos0;
            yy->__thunkpos = yythunkpos0;
            return 0;
        }
    }
    // If mode is "extended", we ensure that the identifier ended
    if( mode == parser_mode_extended )
    {
        if( yy_IdentifierChar(yy) )
        {
            // Don't accept
            yy->__pos = yypos0;
            yy->__thunkpos = yythunkpos0;
            return 0;
        }
    }
    return 1;
}

// Checks the input searching for a token
static int testToken(int turbo_tok, enum enum_tokens e)
{
    const struct tokens *t = &tokens[e];
    int i;
    yycontext *yy = yyctx;
    int yypos0= yy->__pos, yythunkpos0= yy->__thunkpos;
    enum parser_mode mode = parser_get_mode();

    // Skip if dialect is not turbo and token is
    if( turbo_tok && !parsingTurbo() )
        return 0;

    // Check each character
    for(i=0; 0 != t->tok_short[i]; i++)
    {
        if( !matchIgnoreCase(yy, t->tok_short[i]) )
        {
            yy->__pos = yypos0;
            yy->__thunkpos = yythunkpos0;
            return 0;
        }
    }

    // If mode is "extended", we ensure that the identifier ended
    if( mode == parser_mode_extended && i )
    {
        char c = t->tok_short[i-1];
        if( c>='A' && c<='Z' && yy_IdentifierChar(yy) )
        {
            // Don't accept
            yy->__pos = yypos0;
            yy->__thunkpos = yythunkpos0;
            return 0;
        }
    }
    return 1;
}

// Our exported functions
int parse_file(const char *fname)
{
    inFile = fopen(fname, "rb");
    if( !inFile )
    {
        err_print(fname, 0, "%s\n", strerror(errno));
        return 0;
    }
    inc_file_line();
    int e = yyparse();
    yyrelease(yyctx);
    fclose(inFile);
    inFile = 0;
    if( !e )
        err_print(fname, 0, "failed to parse input.\n");
    return e && !get_parse_errors();
}

