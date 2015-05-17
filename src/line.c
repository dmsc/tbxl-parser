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
#include "line.h"
#include "stmt.h"
#include "ataribcd.h"
#include "statements.h"
#include "tokens.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>

///////////////////////////////////////////////////////////////////////
// LINE

struct line_struct {
    enum {
        lt_linenum,
        lt_statement
    } type;
    union {
        int linenum;
        stmt *statement;
    };
};

line *line_new_linenum(int num)
{
    line *l = malloc(sizeof(line));
    l->type = lt_linenum;
    l->linenum = num;
    return l;
}

line *line_new_statement(enum enum_statements s)
{
    line *l = malloc(sizeof(line));
    l->type = lt_statement;
    l->statement = stmt_new(s);
    return l;
}

void line_delete(line *l)
{
    if( l->type == lt_statement )
        stmt_delete( l->statement );
    free(l);
}

int line_is_num(const line *l)
{
    return l->type == lt_linenum;
}

int  line_get_num(const line *l)
{
    if( l->type == lt_linenum )
        return l->linenum;
    else
    {
        fprintf(stderr,"INTERNAL ERROR: not a line number\n");
        abort();
    }
}

stmt *line_get_statement(const line *l)
{
    if( l->type == lt_statement )
        return l->statement;
    else
    {
        fprintf(stderr,"INTERNAL ERROR: not a statement\n");
        abort();
    }
}
