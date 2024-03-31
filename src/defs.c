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
#include "darray.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct def {
    char *name; // Name
    char *data; // Data if string
    int len;    // Length of data
    double val; // Value if numeric
};

// List of all definitions
darray_struct(struct def, defs_struct);

defs * defs_new()
{
    return darray_new(struct def, 16);
}

void defs_delete(defs *d)
{
    int i;
    for(i=0; i<darray_len(d); i++)
    {
        free(darray_i(d,i).name);
        if( darray_i(d,i).data )
            free(darray_i(d,i).data);
    }
    darray_free(d);
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

int defs_search(const defs *d, const char *name)
{
    size_t i;
    for(i=0; i<darray_len(d); i++)
        if( !case_name_cmp(name, darray_i(d,i).name) )
            return i;
    return -1;
}

int defs_new_def(defs *d, const char *name, const char *file_name, int file_line)
{
    int i;
    for(i=0; i<darray_len(d); i++)
        if( !case_name_cmp(name, darray_i(d,i).name) )
            return i;
    struct def df;
    df.name = strdup(name);
    darray_add(d,df);

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

void defs_set_string(defs *d, unsigned id, const char *data, int len)
{
    assert( id >= 0 && id < darray_len(d) );
    darray_i(d,id).data = malloc(len);
    memcpy(darray_i(d,id).data, data, len);
    darray_i(d,id).len = len;
}

void defs_set_numeric(defs *d, unsigned id, double val)
{
    assert( id >= 0 && id < darray_len(d) );
    darray_i(d,id).val = val;
}

const char *defs_get_string(const defs *d, unsigned id, int *len)
{
    assert( id >= 0 && id < darray_len(d) && darray_i(d,id).data );
    *len  = darray_i(d,id).len;
    return darray_i(d,id).data;
}

double defs_get_numeric(const defs *d, unsigned id)
{
    assert( id >= 0 && id < darray_len(d) && !darray_i(d,id).data );
    return darray_i(d,id).val;
}

int defs_get_type(const defs *d, unsigned id)
{
    assert( id >= 0 && id < darray_len(d) );
    return darray_i(d,id).data != 0;
}

const char * defs_get_name(const defs *d, unsigned id)
{
    assert( id >= 0 && id < darray_len(d) );
    return darray_i(d,id).name;
}
