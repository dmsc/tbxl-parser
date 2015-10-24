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
#include "darray.h"
#include <stdlib.h>
#include <string.h>

struct var {
    char *name;    // Long name
    char *sname;   // Short name
    enum var_type type; // Type
};

typedef darray(struct var) var_list;

struct vars_struct {
    var_list vlist;          // Array with all variables
    unsigned num[vtMaxType]; // Number of variables of each type
};

vars * vars_new()
{
    vars *v = calloc(1, sizeof(struct vars_struct));
    darray_init(v->vlist, 64);
    return v;
}

void vars_delete(vars *v)
{
    struct var *vr;
    darray_foreach(vr, &v->vlist)
    {
        free(vr->name);
        free(vr->sname);
    }
    darray_delete(v->vlist);
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
    struct var *vr;
    darray_foreach(vr, &v->vlist)
        if( vr->type == type && !case_name_cmp(name, vr->name) )
            return vr - &darray_i(&v->vlist,0);
    return -1;
}

int vars_get_total(const vars *v)
{
    return darray_len(&v->vlist);
}

int vars_new_var(vars *v, const char *name, enum var_type type, const char *file_name, int file_line)
{
    // Search in available variables
    int i = vars_search(v, name, type);
    if( i>=0 )
        return i;

    char *sname = get_short_name( v->num[type] );

    struct var vr;

    vr.name = strdup(name);
    vr.sname = sname;
    vr.type = type;
    // Get variable number and add to the list
    i = darray_len(&v->vlist);
    darray_add(&v->vlist, vr);
    // Increment number of variables of given type
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
    return darray_i(&v->vlist,id).name;
}

const char *vars_get_short_name(vars *v, int  id)
{
    return darray_i(&v->vlist,id).sname;
}

enum var_type vars_get_type(vars *v, int id)
{
    return darray_i(&v->vlist,id).type;
}

void vars_show_summary(vars *v, enum var_type t, int bin)
{
    const struct var *vr;
    int id = -1;
    darray_foreach(vr, &v->vlist)
    {
        id++;
        if( vr->type == t && case_name_cmp(vr->name, vr->sname) )
        {
            if( bin )
                fprintf(stderr, "\t%03X\t%s\n", id, vr->name);
            else
                fprintf(stderr, "\t%-2s\t%s\n", vr->sname, vr->name);
        }
    }
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
