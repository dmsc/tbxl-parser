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
#include "defs.h"
#include "tokens.h"
#include "statements.h"
#include "dbg.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct def {
    char *name; // Name
    char *data; // Data if string
    int len;    // Length of data
    double val; // Value if numeric
};

#define MAX_DEFS  (512)  // Currently, a fixed limit

struct defs_struct {
  struct def dlist[MAX_DEFS]; // Array with all definitions
};

defs * defs_new()
{
    return calloc(1, sizeof(struct defs_struct));
}

void defs_delete(defs *d)
{
    int i;
    for(i=0; i<MAX_DEFS && d->dlist[i].name; i++)
    {
        free(d->dlist[i].name);
        if( d->dlist[i].data )
            free(d->dlist[i].data);
    }
    free(d);
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

int defs_search(defs *d, const char *name)
{
    int i;
    for(i=0; i<MAX_DEFS && d->dlist[i].name; i++)
        if( !case_name_cmp(name, d->dlist[i].name) )
            return i;
    return -1;
}

int defs_new_def(defs *d, const char *name, const char *file_name, int file_line)
{
    int i;
    for(i=0; i<MAX_DEFS && d->dlist[i].name; i++)
        if( !case_name_cmp(name, d->dlist[i].name) )
            return i;
    if( i >= MAX_DEFS )
    {
        err_print(file_name, file_line, "too many definitions.\n");
        return 0;
    }
    d->dlist[i].name = strdup(name);

    // Search in token list, to avoid defining variables identical to tokens
    int j;
    for(j=0; j<TOK_LAST_TOKEN; j++)
        if( !case_name_cmp(name, tokens[j].tok_in) )
        {
            warn_print(file_name, file_line, "definition name '%s' is a token\n", name);
            break;
        }
    for(j=0; j<STMT_ENDIF_INVISIBLE; j++)
        if( !case_name_cmp(name, statements[j].stm_long) )
        {
            warn_print(file_name, file_line, "definition name '%s' is a statement\n", name);
            break;
        }
    for(j=0; j<TOK_LAST_TOKEN; j++)
        if( !case_name_cmp_str(name, tokens[j].tok_in) )
        {
            warn_print(file_name, file_line, "definition name '%s$' is a token\n", name);
            break;
        }

    return i;
}

void defs_set_string(defs *d, int id, const char *data, int len)
{
    assert( id >= 0 && id < MAX_DEFS && d->dlist[id].name );
    d->dlist[id].data = malloc(len);
    memcpy(d->dlist[id].data, data, len);
    d->dlist[id].len = len;
}

void defs_set_numeric(defs *d, int id, double val)
{
    assert( id >= 0 && id < MAX_DEFS && d->dlist[id].name );
    d->dlist[id].val = val;
}

int defs_get_string(defs *d, int id, const char **data, int *len)
{
    if( id < 0 || id >= MAX_DEFS || ! d->dlist[id].name || ! d->dlist[id].data )
        return 0;
    *data = d->dlist[id].data;
    *len  = d->dlist[id].len;
    return 1;
}

int defs_get_numeric(defs *d, int id, double *val)
{
    if( id < 0 || id >= MAX_DEFS || ! d->dlist[id].name || d->dlist[id].data )
        return 0;
    *val = d->dlist[id].val;
    return 1;
}

int defs_get_type(const defs *d, int id)
{
    assert( id >= 0 && id < MAX_DEFS && d->dlist[id].name );
    return d->dlist[id].data != 0;
}

const char * defs_get_name(const defs *d, int id)
{
    assert( id >= 0 && id < MAX_DEFS && d->dlist[id].name );
    return d->dlist[id].name;
}
