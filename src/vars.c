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
#include "vars.h"
#include "tokens.h"
#include "statements.h"
#include "dbg.h"
#include <stdlib.h>
#include <string.h>

struct var {
    char *name;    // Long name
    char *sname;   // Short name
    enum var_type type; // Type
};

// TurboBasic allows for 256 variables
#define maxVars 256

struct vars_struct {
  struct var vlist[maxVars]; // Array with all variables
  unsigned num[vtMaxType];   // Number of variables of each type
};

vars * vars_new()
{
    vars *v = calloc(1, sizeof(struct vars_struct));
    return v;
}

void vars_delete(vars *v)
{
    int i;
    for(i=0; i<maxVars && v->vlist[i].name; i++)
    {
        free(v->vlist[i].name);
        free(v->vlist[i].sname);
    }
    free(v);
}

static int case_name_cmp(const char *a, const char *b)
{
    for( ; *a ; ++a, ++b )
    {
        char ca = *a & 0x7F, cb = *b & 0x7F;
        ca = (ca>='a' && ca<='z') ? ca+'A'-'a' : ca;
        cb = (cb>='a' && cb<='z') ? cb+'A'-'a' : cb;
        if( ca != cb )
            return 1;
    }
    return *b != 0;
}

// Compare a variable name without the "$" to a name *with* the "$"
static int case_name_cmp_str(const char *a, const char *b)
{
    for( ; *a ; ++a, ++b )
    {
        char ca = *a & 0x7F, cb = *b & 0x7F;
        ca = (ca>='a' && ca<='z') ? ca+'A'-'a' : ca;
        cb = (cb>='a' && cb<='z') ? cb+'A'-'a' : cb;
        if( ca != cb )
            return 1;
    }
    return *b != '$';
}

static char *get_short_name(int n)
{
    if( n > 1020 )
    {
        // Error, too many variables!
        return 0;
    }
    if( n < 27 )
    {
        char *out = malloc(2);
        out[0] = n==26 ? '_' : 'A'+n;
        out[1] = 0;
        return out;
    }
    if( n > 161 )
        n++;     // Skip 161 - "DO"
    if( n > 337 )
        n++;     // Skip 337 - "IF"
    if( n > 567 )
        n++;     // Skip 567 - "ON"
    if( n > 571 )
        n++;     // Skip 571 - "OR"
    if( n > 753 )
        n++;     // Skip 753 - "TO"
    int c1 = (n-27) / 37;
    int c2 = (n-27) % 37;
    char *out = malloc(3);
    out[0] = c1==26 ? '_' : 'A'+c1;
    out[1] = c2<10 ? '0'+c2 : (c2==36 ? '_' : 'A'+c2-10);
    out[2] = 0;
    return out;
}

int vars_search(vars *v, const char *name, enum var_type type)
{
    int i;
    for(i=0; i<maxVars && v->vlist[i].name; i++)
        if( v->vlist[i].type == type && !case_name_cmp(name, v->vlist[i].name) )
            return i;
    return -1;
}

int vars_get_total(const vars *v)
{
    int i;
    for(i=0; i<maxVars && v->vlist[i].name; i++);
    return i;
}

int vars_new_var(vars *v, const char *name, enum var_type type, const char *file_name, int file_line)
{
    int i;

    // Search in available variables
    for(i=0; i<maxVars && v->vlist[i].name; i++)
        if( v->vlist[i].type == type && !case_name_cmp(name, v->vlist[i].name) )
            return i;

    char *sname = get_short_name( v->num[type] );

    if( i == maxVars || !sname )
        return -1;

    v->vlist[i].name = strdup(name);
    v->vlist[i].sname = sname;
    v->vlist[i].type = type;
    v->num[type] ++;

    // End if called from outside program, don't check name.
    if( !file_name || file_line < 0 )
        return i;

    // Search in token list, to avoid defining variables identical to tokens
    if( type == vtFloat || type == vtArray )
    {
        int j;
        for(j=0; j<TOK_LAST_TOKEN; j++)
            if( !case_name_cmp(name, tokens[j].tok_in) )
            {
                warn_print(file_name, file_line, "variable name '%s' is a token\n", name);
                break;
            }
        for(j=0; j<STMT_ENDIF_INVISIBLE; j++)
            if( !case_name_cmp(name, statements[j].stm_long) )
            {
                warn_print(file_name, file_line, "variable name '%s' is a statement\n", name);
                break;
            }
    }
    else if( type == vtString )
    {
        int j;
        for(j=0; j<TOK_LAST_TOKEN; j++)
            if( !case_name_cmp_str(name, tokens[j].tok_in) )
            {
                warn_print(file_name, file_line, "variable name '%s$' is a token\n", name);
                break;
            }
    }

    return i;
}

int vars_get_count(vars *v, enum var_type type)
{
    return v->num[type];
}

const char *vars_get_long_name(vars *v, int id)
{
    return v->vlist[id].name;
}

const char *vars_get_short_name(vars *v, int  id)
{
    return v->vlist[id].sname;
}

enum var_type vars_get_type(vars *v, int id)
{
    return v->vlist[id].type;
}

const char *var_type_name(enum var_type t)
{
    switch( t )
    {
        case vtFloat:
            return "float";
        case vtString:
            return "string";
        case vtArray:
            return "array";
        case vtLabel:
            return "label";
        default:
            return "<ERROR>";
    }
}
