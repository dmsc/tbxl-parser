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
#include "dmem.h"
#include "darray.h"
#include "parser.h"
#include <stdlib.h>
#include <string.h>

// 27 characters for the first letter, 37 characters for the second letter
// and 5 names already reserved: "DO", "IF", "ON", "OR" and "TO".
#define MAX_SHORT_NAMES (27 * 37 + 27 - 5)

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

vars * vars_new(void)
{
    vars *v = dcalloc(1, sizeof(struct vars_struct));
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

// Compares A and B ignoring case and inverse video.
// Returns 1 if A != B, 0 if A == B.
// If "prefix" is 1, returns 0 also if B is a prefix of A.
static int case_name_cmp(const char *a, const char *b, int prefix)
{
    for( ; *a ; ++a, ++b )
    {
        char ca = *a & 0x7F, cb = *b & 0x7F;
        ca = (ca>='a' && ca<='z') ? ca+'A'-'a' : ca;
        cb = (cb>='a' && cb<='z') ? cb+'A'-'a' : cb;
        if( ca != cb )
            return !prefix || *b != 0;
    }
    return *b != 0;
}

// Compare a variable name without the "$" to a name *with* the "$"
// Returns 1 if A+"$" != B, 0 if A+"$" == B.
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

// Builds a short variable name for Atari BASIC
static char *get_short_name_abas(int n)
{
    // In Atari BASIC, we have less names available.
    if( n >= (26 * 36 + 26 - 4) )
    {
        // Error, too many variables!
        return 0;
    }
    if( n < 26 )
    {
        char *out = dmalloc(2);
        out[0] = 'A'+n;
        out[1] = 0;
        return out;
    }
    if( n > 730 )
        n++;     // Skip 731 - "TO"
    if( n > 554 )
        n++;     // Skip 555 - "OR"
    if( n > 551 )
        n++;     // Skip 552 - "ON"
    if( n > 328 )
        n++;     // Skip 329 - "IF"
    int c1 = (n-26) / 36;
    int c2 = (n-26) % 36;
    char *out = dmalloc(3);
    out[0] = 'A'+c1;
    out[1] = c2<10 ? '0'+c2 : 'A'+c2-10;
    out[2] = 0;
    return out;
}


static char *get_short_name_tbxl(int n)
{
    if( n >= MAX_SHORT_NAMES )
    {
        // Error, too many variables!
        return 0;
    }
    if( n < 27 )
    {
        char *out = dmalloc(2);
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
    char *out = dmalloc(3);
    out[0] = c1==26 ? '_' : 'A'+c1;
    out[1] = c2<10 ? '0'+c2 : (c2==36 ? '_' : 'A'+c2-10);
    out[2] = 0;
    return out;
}

// Gets the "index" of the character (A-Z + _ + 0-9)
static int get_char_index(char c, int digit)
{
    int add = digit ? 10 : 0;
    if( c >= 'A' && c <= 'Z' )
        return c - 'A' + add;
    else if( c >= 'a' && c <= 'z' )
        return c - 'a' + add;
    else if( c == '_' )
        return 26 + add;
    else if( digit && c >= '0' && c <= '9' )
        return c - '0';
    else
        return -1; // Unknown character
}

// This is exact inverse of above function
static int get_short_index_abas(const char *name)
{
    if( !name || !name[0] || (name[0] && name[1] && name[2]) )
        return -1; // Name too long or null

    if( name[0] == '_' || name[1] == '_' )
        return -1; // Invalid in Atari BASIC

    if( !name[1] )
        return get_char_index(name[0], 0);
    else
    {
        int i1 = get_char_index(name[0], 0);
        int i2 = get_char_index(name[1], 1);
        if( i1 < 0 || i2 < 0 )
            return -1;
        int n = 26 + 36 * i1 + i2;
        if( n < 329 )
            return n;
        else if( n == 329 )
            return -1; // "IF"
        else if( n < 553 )
            return n - 1;
        else if( n == 553 )
            return -1; // "ON"
        else if( n < 557 )
            return n - 2;
        else if( n == 557 )
            return -1; // "OR"
        else if( n < 734 )
            return n - 3;
        else if( n == 734 )
            return -1; // "TO"
        else
            return n - 4;
    }
}

static int get_short_index_tbxl(const char *name)
{
    if( !name || !name[0] || (name[0] && name[1] && name[2]) )
        return -1; // Name too long or null

    if( !name[1] )
        return get_char_index(name[0], 0);
    else
    {
        int i1 = get_char_index(name[0], 0);
        int i2 = get_char_index(name[1], 1);
        if( i1 < 0 || i2 < 0 )
            return -1;
        int n = 27 + 37 * i1 + i2;
        if( n < 162 )
            return n;
        else if( n == 162 )
            return -1; // "DO"
        else if( n < 338 )
            return n - 1;
        else if( n == 338 )
            return -1; // "IF"
        else if( n < 568 )
            return n - 2;
        else if( n == 568 )
            return -1; // "ON"
        else if( n < 572 )
            return n - 3;
        else if( n == 572 )
            return -1; // "OR"
        else if( n < 754 )
            return n - 4;
        else if( n == 754 )
            return -1; // "TO"
        else
            return n - 5;
    }
}

static char *get_short_name(int n)
{
    if(parser_get_dialect() == parser_dialect_turbo)
        return get_short_name_tbxl(n);
    else
        return get_short_name_abas(n);
}

static int get_short_index(const char *name)
{
    if(parser_get_dialect() == parser_dialect_turbo)
        return get_short_index_tbxl(name);
    else
        return get_short_index_abas(name);
}

int vars_search(vars *v, const char *name, enum var_type type)
{
    struct var *vr;
    darray_foreach(vr, &v->vlist)
        if( vr->type == type && !case_name_cmp(name, vr->name, 0) )
            return vr - &darray_i(&v->vlist,0);
    return -1;
}

int vars_get_total(const vars *v)
{
    return darray_len(&v->vlist);
}

// Assign the short names to variables
void vars_assign_short_names(vars *v)
{
    char used[vtMaxType][MAX_SHORT_NAMES]; // Stores if variable is already used
    int index[vtMaxType];
    struct var *vr;
    // Cleanup
    memset(used, 0, sizeof(used));
    memset(index, 0, sizeof(index));
    // First, delete old names
    darray_foreach(vr, &v->vlist)
    {
        free(vr->sname);
        vr->sname = 0;
    }
    // Now, try assigning names that are of 1 or 2 chars:
    darray_foreach(vr, &v->vlist)
    {
        int id = get_short_index( vr->name );
        if( id >= 0 && id < MAX_SHORT_NAMES && !used[vr->type][id] )
        {
            vr->sname = get_short_name(id);
            used[vr->type][id] = 1;
        }
    }
    // And assign the rest
    darray_foreach(vr, &v->vlist)
    {
        if( !vr->sname )
        {
            int id = index[vr->type];
            for( ; id < MAX_SHORT_NAMES ; id++ )
            {
                if( used[vr->type][id] )
                    continue;
                vr->sname = get_short_name(id);
                used[vr->type][id] = 1;
                break;
            }
            index[vr->type] = id;
        }
    }
    // Check errors
    int t=0;
    for(t=0; t<vtMaxType; t++)
    {
        if( index[t] >= MAX_SHORT_NAMES )
            err_print("", 0, "too many variables of type %s, could not assign short names\n",
                      var_type_name(t));
    }
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
    int warned = 0;
    if( type == vtFloat || type == vtArray )
    {
        int j;
        for(j=0; j<TOK_LAST_TOKEN; j++)
            if( !case_name_cmp(name, tokens[j].tok_in, 0) )
            {
                warn_print(file_name, file_line, "variable name '%s' is a token\n", name);
                warned = 1;
                break;
            }
        for(j=0; j<STMT_ENDIF_INVISIBLE; j++)
            if( !case_name_cmp(name, statements[j].stm_long, 0) )
            {
                warn_print(file_name, file_line, "variable name '%s' is a statement\n", name);
                warned = 1;
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
                warned = 1;
                break;
            }
    }
    // If we are parsing in "compatible" mode, warn if variable name is a prefix
    // of a statement
    if( !warned && type != vtLabel && parser_get_mode() != parser_mode_extended )
    {
        int j;
        for(j=0; j<STMT_ENDIF_INVISIBLE; j++)
            if( statements[j].stm_long[0] && !case_name_cmp(name, statements[j].stm_long, 1) )
            {
                warn_print(file_name, file_line,
                        "variable name '%s%s' starts with statement '%s'\n",
                        name, type == vtString ? "$" : "", statements[j].stm_long);
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
        if( vr->type == t && (!vr->sname || case_name_cmp(vr->name, vr->sname, 0)) )
        {
            if( bin || !vr->sname )
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
