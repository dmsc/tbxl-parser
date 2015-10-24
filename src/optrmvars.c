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

#include "optrmvars.h"
#include "expr.h"
#include "vars.h"
#include "dbg.h"
#include "program.h"
#include "darray.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Struct to hold constant value and count
typedef struct {
    const char *name;  // Long name
    unsigned written;  // Times written (or label defined)
    unsigned read;     // Times read (used in expression)
    unsigned total;    // Total usage
    int new_id;        // New id assigned to the variable
    enum var_type type;// Type of variable
} var_usage;

typedef darray(var_usage) var_list;

// Check if an expr is a variable
static int expr_is_var(const expr *ex)
{
    return ex->type == et_var_number || ex->type == et_var_string ||
           ex->type == et_var_label  || ex->type == et_var_array;
}

// Assign new IDs to variables, most used variables first
static void var_list_assign_new_id(var_list *vl, vars *nvar, const char *fname)
{
    int num = darray_len(vl);
    int *idx = malloc(sizeof(int) * num);
    int nused = 0;

    // Assign indexes to used variables
    for(int i=0; i<num; i++)
    {
        if( darray_i(vl, i).total )
        {
            idx[nused] = i;
            nused++;
        }
    }
    for(int i=nused; i<num; i++)
        idx[i] = -1;

    if( num != nused )
        info_print(fname, 0, "removing %d unused variables.\n", num - nused);

    // Only sort variables if there is a gain in doing so, in this case, only
    // if we have more than 127 variables.
    if( nused > 127 )
    {
        for(int i=1; i<nused; i++)
        {
            int j = i, tmp = idx[j], total = darray_i(vl, tmp).total;
            for( ; j>0 && darray_i(vl, idx[j-1]).total < total; j--)
                idx[j] = idx[j-1];
            idx[j] = tmp;
        }
    }

    // Now we have the variable indexes sorted in "idx", use them
    // to assign new index
    for(int i=0; i<nused; i++)
    {
        assert( idx[i]>=0 && idx[i]<num );
        var_usage *vu = &darray_i(vl, idx[i]);
        vu->new_id = vars_new_var(nvar, vu->name, vu->type, 0, 0);
        assert(vu->new_id == i);
    }

    free(idx);
}

// Replace variable IDs with the new id assigned
static int do_replace_var_id(expr *ex, var_list *vl)
{
    int err = 0;

    if( ex )
    {
        err |= do_replace_var_id(ex->lft, vl);
        err |= do_replace_var_id(ex->rgt, vl);

        if( expr_is_var(ex) )
        {
            assert(ex->var>=0 && ex->var<darray_len(vl));
            ex->var = darray_i(vl, ex->var).new_id;
        }
    }
    return err;
}

// Count the usage of each variable
static int do_get_var_usage(expr *ex, var_list *vl)
{
    int err = 0;

    if( ex )
    {
        err |= do_get_var_usage(ex->lft, vl);
        err |= do_get_var_usage(ex->rgt, vl);

        if( expr_is_var(ex) )
        {
            assert(ex->var>=0 && ex->var<darray_len(vl));
            darray_i(vl, ex->var).total ++;
        }
    }
    return err;
}


int opt_remove_unused_vars(expr *prog)
{
    if( !prog )
        return 0;

    // Create the array with variable statistics
    var_list *vl = darray_new(var_usage, 16);
    vars *v = pgm_get_vars( expr_get_program(prog) );
    for(int i=0; i<vars_get_total(v); i++)
    {
        var_usage vu;
        vu.name = vars_get_long_name(v, i);
        vu.type = vars_get_type(v, i);
        vu.read = vu.written = vu.total = 0;
        darray_add(vl,vu);
    }

    do_get_var_usage(prog, vl);

    // Now, recreate variable list!
    vars *nvar = vars_new();
    var_list_assign_new_id(vl, nvar, expr_get_file_name(prog));

    // Replace variable ids in expressions
    do_replace_var_id(prog, vl);

    // Replace in program
    pgm_set_vars( expr_get_program(prog), nvar);

    darray_free(vl);
    return 0;
}

