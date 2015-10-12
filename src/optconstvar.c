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

#include "optconstvar.h"
#include "optexpr.h"
#include "vars.h"
#include "dbg.h"
#include "program.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Simplify printing warnings
#define warn(...) \
    do { warn_print(expr_get_file_name(ex), expr_get_file_line(ex), __VA_ARGS__); } while(0)

static void memory_error(void)
{
    fprintf(stderr,"INTERNAL ERROR: memory allocation failure.\n");
    abort();
}

// Struct to hold constant value and count
typedef struct {
    unsigned count;  // Times repeated
    int vid;         // Variable id assigned
    int status;      // Status: 0 = unused, 1 = variable assigned, 2 = code emitted
    const uint8_t *str; // String value, NULL if not a string
    unsigned slen;   // String length
    double num;      // Value, valid if str == NULL
} cvalue;


// Compare two constants to search for repeated. Returns 0 if equal.
static int cvalue_comp(const cvalue *a, const cvalue *b)
{
    // If both are number, compare values:
    if( !a->str && !b->str )
        return a->num != b->num;
    // If both string, compare values:
    else if( a->str && b->str )
    {
        if( a->slen != b->slen )
            return 1;
        return memcmp(a->str, b->str, a->slen);
    }
    // Else, they are different
    else
        return 1;
}

// Function to estimate the number of bytes saved by factorizing this constant
static int cvalue_saved_bytes(const cvalue *c)
{
    if( c->str )
    {
        // Currently, the constant uses 2+LEN bytes each time it
        // appears in the program, by replacing by a variable we
        // use 1 byte each time is used (if variable is < 128),
        // so we save "count * (1+slen)" bytes.
        //
        // To use the constant, we need to add the size of the
        // variable (6 + slen bytes), the size of the name (we
        // assume best case, 0 bytes) and the size of the init
        // program:
        //     DIM X$(LEN) : X$="....."
        // Assuming the LEN is not a common value, we have:
        // LEN=0, 1, 2 or 3: 13 + slen + 6 + slen
        // LEN>3           : 19 + slen + 6 + slen
        //
        if( c->slen < 3 )
            return (19 + 2 * c->slen) - c->count * (1+c->slen);
        else
            return (25 + 2 * c->slen) - c->count * (1+c->slen);
    }
    else
    {
        double n = c->num;
        // Currently, the constant uses 7 bytes each time, it
        // appears in the program, by replacing by a variable we
        // use 1 byte each time is used (if variable is < 128),
        // so we save "count * 6" bytes.
        //
        // To use the constant, we need to add the size of the
        // variable (6 bytes), the size of the name (we assume
        // best case, 0 bytes) and the size of the init program,
        // one of:
        //    X = NUM
        //    X = -%N
        //    X =  %N1 op %N2
        //    X = -%N1 op %N2
        //    X =  %N1 op %N2 op %N3
        // The options are:
        //  N=-1, -2, -3                          : 4 + 2 =  6 bytes
        //  N=1/3,1/2,2/3,3/2,4, 5, 6, 9          : 4 + 3 =  7 bytes
        //  N=-1/3,-1/2,-2/3,-3/2,-4,-5,-6,-9     : 4 + 4 =  8 bytes
        //  N=-8,-7,4/3,5/3,7/3,5/2,8/3,10/3,
        //    7/2,11/3,9/2,7,8,10,11,12,18,27     : 4 + 5 =  9 bytes
        //  N=-27,-18,-12,-11,-10,-9/2,-11/3,-7/2,
        //    -10/3,-8/3,-5/2,-7/3,-5/3,-4/3      : 4 + 6 = 10 bytes
        //  other N                               : 4 + 7 = 11 bytes
        if( n == -1 || n == -2 || n == -3 )
            return 12 - c->count * 6;
        else if( n == 4 || n == 5 || n == 6 || n == 9 || n == 0.5 ||
                 n == 1/3.0 || n == 2/3.0 || n == 1.5 )
            return 13 - c->count * 6;
        else if( n == -4 || n == -5 || n == -6 || n == -9 || n == -0.5 ||
                 n == -1/3.0 || n == -2/3.0 || n == -1.5 )
            return 14 - c->count * 6;
        else if( n == -8 || n == -7 || n == 4/3.0 || n == 5/3.0 || n == 7/3.0 ||
                 n == 2.5 || n == 8/3.0 || n == 10/3.0 || n == 7/2.0 || n == 11/3.0 ||
                 n == 4.5 || n == 7 || n == 8 || n == 9 || n == 10 || n == 11 ||
                 n == 12 || n == 18 || n == 27 )
            return 15 - c->count * 6;
        else if( n == -27 || n == -18 || n == -12 || n == -11 || n == -10 ||
                 n == -4.5 || n == -11/3.0 || n == -3.5 || n == -10/3.0 || n ==  -8/3.0||
                 n == -2.5 || n == -7/3.0 || n == -5/3.0 || n == -4/3.0 )
            return 16 - c->count * 6;
        else
            return 17 - c->count * 6;
    }
}

// Function to sort constant values based on bytes saved
static int cvalue_sort_comp(const void *pa, const void *pb)
{
    // Calculate the number of bytes for "a" and "b"
    int sa = cvalue_saved_bytes(pa);
    int sb = cvalue_saved_bytes(pb);
    return sa-sb;
}

// List of cvalue
typedef struct {
    cvalue *data;
    unsigned len;
    unsigned size;
} cvalue_list;

static cvalue_list *cvalue_list_new(void)
{
    cvalue_list *l = malloc(sizeof(cvalue_list));
    if( !l || !(l->data = malloc(sizeof(cvalue) * 256)) )
        memory_error();
    l->len = 0;
    l->size = 256;
    return l;
}

static void cvalue_list_delete(cvalue_list *l)
{
    free(l->data);
    free(l);
}

static void cvalue_list_add(cvalue_list *l, cvalue *nv)
{
    if( l->len == l->size )
    {
        l->size *= 2;
        if( !l->size || !(l->data = realloc(l->data, sizeof(cvalue) * l->size)) )
            memory_error();
    }
    memcpy(l->data + l->len, nv, sizeof(*nv));
    l->len ++;
}

static cvalue *cvalue_list_find(cvalue_list *l, cvalue *nv)
{
    unsigned i;
    for(i=0; i<l->len; i++)
        if( 0 == cvalue_comp(nv, l->data + i) )
            return l->data + i;
    return 0;
}

static void cvalue_list_sort(cvalue_list *l)
{
    qsort(l->data, l->len, sizeof(l->data[0]), cvalue_sort_comp);
}

static int expr_is_cnum(const expr *ex)
{
    return ex && (ex->type == et_c_number || ex->type == et_c_hexnumber);
}

static int expr_is_cstr(const expr *ex)
{
    return ex && ex->type == et_c_string;
}

// Update constant value. Returns current count of constant values found and updates tree
static int update_cvalue(const expr *ex, cvalue_list *l)
{
    cvalue val;
    memset(&val, 0, sizeof(val));

    if( expr_is_cnum(ex) )
    {
        val.num = ex->num;
    }
    else if( expr_is_cstr(ex) )
    {
        val.str = ex->str;
        val.slen = ex->slen;
    }
    else if( ex )
        return update_cvalue(ex->lft, l) + update_cvalue(ex->rgt, l);
    else
        return 0;

    cvalue *n = cvalue_list_find(l, &val);
    if( n )
    {
        n->count ++;
        return 0;
    }
    else
    {
        // Insert new value with count == 1
        val.count = 1;
        cvalue_list_add(l, &val);
        return 1;
    }
}

// Replace constant value with variable. Returns number of times replaced
static int replace_cvalue(expr *ex, cvalue *cv)
{
    if( !ex )
        return 0;
    else if( expr_is_cnum(ex) )
    {
        if( !cv->str && ex->num == cv->num )
        {
            ex->type = et_var_number;
            ex->var  = cv->vid;
            return 1;
        }
        else
            return 0;
    }
    else if( expr_is_cstr(ex) )
    {
        if( cv->str && cv->slen == ex->slen && 0 == memcmp(ex->str, cv->str, ex->slen) )
        {
            // NOTE: we don't free the string data, will be freed in the expr manager
            ex->type = et_var_string;
            ex->var  = cv->vid;
            return 1;
        }
        else
            return 0;
    }
    else if( ex )
        return replace_cvalue(ex->lft, cv) + replace_cvalue(ex->rgt, cv);
    else
        return 0;

}

static int val_from_i(expr_mngr *m, cvalue_list *l, int i, double *v)
{
    if( i < 4 )
    {
        *v = i;
        return i+1;
    }
    unsigned j = i - 4;
    while(j<l->len)
    {
        const cvalue *cv = l->data + j;
        j++;
        if( !cv->str && cv->status == 2 )
        {
            *v = cv->num;
            return j + 4;
        }
    }
    return 0;
}

static expr *expr_from_i(expr_mngr *m, cvalue_list *l, int i)
{
    if( i == 0 ) return expr_new_tok(m, TOK_PER_0);
    if( i == 1 ) return expr_new_tok(m, TOK_PER_1);
    if( i == 2 ) return expr_new_tok(m, TOK_PER_2);
    if( i == 3 ) return expr_new_tok(m, TOK_PER_3);
    const cvalue *cv = l->data + i - 4;
    return expr_new_var_num(m, cv->vid);
}

static expr *create_num(expr_mngr *m, cvalue_list *l, double n)
{
    int i;
    double x;
    // Creates the optimal numeric initialization expression
    // First, try standard tokens:
    for(i=0; 0 != (i = val_from_i(m, l, i, &x)); )
    {
        if( n == x )
            return expr_from_i(m, l, i-1);
    }

    // Now, try with "MINUS" and a value:
    for(i=0; 0 != (i = val_from_i(m, l, i, &x)); )
    {
        if( n == -x )
            return expr_new_uni(m, expr_from_i(m, l, i-1), TOK_UMINUS);
    }

    // Now, try with operations between two simple values:
    for(i=0; 0 != (i = val_from_i(m, l, i, &x)); )
    {
        int j;
        double y;
        for(j=0; 0 != (j = val_from_i(m, l, j, &y)); )
        {
            if( n == x + y )
                return expr_new_bin(m, expr_from_i(m, l, i-1), expr_from_i(m, l, j-1), TOK_PLUS);
            if( n == x - y )
                return expr_new_bin(m, expr_from_i(m, l, i-1), expr_from_i(m, l, j-1), TOK_MINUS);
            if( n == x * y )
                return expr_new_bin(m, expr_from_i(m, l, i-1), expr_from_i(m, l, j-1), TOK_STAR);
            if( n == x / y )
                return expr_new_bin(m, expr_from_i(m, l, i-1), expr_from_i(m, l, j-1), TOK_SLASH);
        }
    }

    // Same as before, adding "-":
    for(i=0; 0 != (i = val_from_i(m, l, i, &x)); )
    {
        int j;
        double y;
        for(j=0; 0 != (j = val_from_i(m, l, j, &y)); )
        {
            if( n == -x + y )
                return expr_new_bin(m, expr_new_uni(m, expr_from_i(m, l, i-1), TOK_UMINUS), expr_from_i(m, l, j-1), TOK_PLUS);
            if( n == -x - y )
                return expr_new_bin(m, expr_new_uni(m, expr_from_i(m, l, i-1), TOK_UMINUS), expr_from_i(m, l, j-1), TOK_MINUS);
            if( n == -x * y )
                return expr_new_bin(m, expr_new_uni(m, expr_from_i(m, l, i-1), TOK_UMINUS), expr_from_i(m, l, j-1), TOK_STAR);
            if( n == -x / y )
                return expr_new_bin(m, expr_new_uni(m, expr_from_i(m, l, i-1), TOK_UMINUS), expr_from_i(m, l, j-1), TOK_SLASH);
        }
    }

    // Now, try with operations between tree simple values:
    for(i=0; 0 != (i = val_from_i(m, l, i, &x)); )
    {
        int j;
        double y;
        for(j=0; 0 != (j = val_from_i(m, l, j, &y)); )
        {
            int k;
            double z;
            for(k=0; 0 != (k = val_from_i(m, l, k, &z)); )
            {
                if( n == x + y + z )
                    return expr_new_bin(m, expr_new_bin(m, expr_from_i(m,l,i-1), expr_from_i(m,l,j-1), TOK_PLUS), expr_from_i(m,l,k-1), TOK_PLUS);
                if( n == x - y + z )
                    return expr_new_bin(m, expr_new_bin(m, expr_from_i(m,l,i-1), expr_from_i(m,l,j-1), TOK_MINUS), expr_from_i(m,l,k-1), TOK_PLUS);
                if( n == x - y - z )
                    return expr_new_bin(m, expr_new_bin(m, expr_from_i(m,l,i-1), expr_from_i(m,l,j-1), TOK_MINUS), expr_from_i(m,l,k-1), TOK_MINUS);
                if( n == x * y + z)
                    return expr_new_bin(m, expr_new_bin(m, expr_from_i(m,l,i-1), expr_from_i(m,l,j-1), TOK_STAR), expr_from_i(m,l,k-1), TOK_PLUS);
                if( n == x * y - z)
                    return expr_new_bin(m, expr_new_bin(m, expr_from_i(m,l,i-1), expr_from_i(m,l,j-1), TOK_STAR), expr_from_i(m,l,k-1), TOK_MINUS);
                if( n == x * y * z)
                    return expr_new_bin(m, expr_new_bin(m, expr_from_i(m,l,i-1), expr_from_i(m,l,j-1), TOK_STAR), expr_from_i(m,l,k-1), TOK_STAR);
                if( n == x / y + z )
                    return expr_new_bin(m, expr_new_bin(m, expr_from_i(m,l,i-1), expr_from_i(m,l,j-1), TOK_SLASH), expr_from_i(m,l,k-1), TOK_PLUS);
                if( n == x / y - z )
                    return expr_new_bin(m, expr_new_bin(m, expr_from_i(m,l,i-1), expr_from_i(m,l,j-1), TOK_SLASH), expr_from_i(m,l,k-1), TOK_MINUS);
            }
        }
    }

    // Same as before, adding "-":
    for(i=0; 0 != (i = val_from_i(m, l, i, &x)); )
    {
        int j;
        double y;
        for(j=0; 0 != (j = val_from_i(m, l, j, &y)); )
        {
            int k;
            double z;
            for(k=0; 0 != (k = val_from_i(m, l, k, &z)); )
            {
                if( n == -x + y + z )
                    return expr_new_bin(m, expr_new_bin(m, expr_new_uni(m, expr_from_i(m,l,i-1), TOK_UMINUS), expr_from_i(m,l,j-1), TOK_PLUS), expr_from_i(m,l,k-1), TOK_PLUS);
                if( n == -x - y + z )
                    return expr_new_bin(m, expr_new_bin(m, expr_new_uni(m, expr_from_i(m,l,i-1), TOK_UMINUS), expr_from_i(m,l,j-1), TOK_MINUS), expr_from_i(m,l,k-1), TOK_PLUS);
                if( n == -x - y - z )
                    return expr_new_bin(m, expr_new_bin(m, expr_new_uni(m, expr_from_i(m,l,i-1), TOK_UMINUS), expr_from_i(m,l,j-1), TOK_MINUS), expr_from_i(m,l,k-1), TOK_MINUS);
                if( n == -x * y + z)
                    return expr_new_bin(m, expr_new_bin(m, expr_new_uni(m, expr_from_i(m,l,i-1), TOK_UMINUS), expr_from_i(m,l,j-1), TOK_STAR), expr_from_i(m,l,k-1), TOK_PLUS);
                if( n == -x * y - z)
                    return expr_new_bin(m, expr_new_bin(m, expr_new_uni(m, expr_from_i(m,l,i-1), TOK_UMINUS), expr_from_i(m,l,j-1), TOK_STAR), expr_from_i(m,l,k-1), TOK_MINUS);
                if( n == -x * y * z)
                    return expr_new_bin(m, expr_new_bin(m, expr_new_uni(m, expr_from_i(m,l,i-1), TOK_UMINUS), expr_from_i(m,l,j-1), TOK_STAR), expr_from_i(m,l,k-1), TOK_STAR);
                if( n == -x / y + z )
                    return expr_new_bin(m, expr_new_bin(m, expr_new_uni(m, expr_from_i(m,l,i-1), TOK_UMINUS), expr_from_i(m,l,j-1), TOK_SLASH), expr_from_i(m,l,k-1), TOK_PLUS);
                if( n == -x / y - z )
                    return expr_new_bin(m, expr_new_bin(m, expr_new_uni(m, expr_from_i(m,l,i-1), TOK_UMINUS), expr_from_i(m,l,j-1), TOK_SLASH), expr_from_i(m,l,k-1), TOK_MINUS);
            }
        }
    }

    // No simpler expression found:
    return expr_new_number(m, n);
}

static expr *create_num_assign(expr_mngr *m, cvalue_list *l, expr *prev, double x, int vid)
{
    expr *toks = expr_new_bin(m, expr_new_var_num(m, vid), create_num(m, l, x), TOK_F_ASGN);
    return expr_new_stmt(m, prev, toks, STMT_LET_INV);
}

static expr *create_str_dim(expr_mngr *m, cvalue_list *l, expr *exp, const uint8_t *data, unsigned len, int vid)
{
    // Create the DIM expression:  [,] X$(len)
    expr *dim = expr_new_bin(m, expr_new_var_str(m, vid), create_num(m, l, len), TOK_DS_L_PRN);
    if( exp )
        return expr_new_bin(m, exp, dim, TOK_COMMA);
    else
        return dim;
}

static expr *create_str_assign(expr_mngr *m, cvalue_list *l, expr *prev, const uint8_t *data, unsigned len, int vid)
{
    // Create the assign statement: X$=""
    return expr_new_stmt(m, prev, expr_new_bin(m, expr_new_var_str(m, vid), expr_new_string(m, data, len), TOK_S_ASGN), STMT_LET_INV);
}

static void add_to_prog(expr *prog, expr *e)
{
    while(prog->lft)
        prog = prog->lft;
    prog->lft = e;
}

void opt_replace_const(expr *prog)
{
    if( !prog )
        return;

    // Get current number of variables, used to estimate the
    // memory usage of each new constant added to the program
    vars *v = pgm_get_vars( expr_get_program(prog) );
    unsigned nfloat  = vars_get_count(v, vtFloat);
    unsigned nstring = vars_get_count(v, vtString);
    unsigned nvar    = vars_get_total(v);

    // If not enough variables, exit
    if( nvar > 255 )
        return;

    // Search all constant values in the program and store
    // the value and number of times repeated
    cvalue_list *lst = cvalue_list_new();
    int num = update_cvalue(prog, lst);

    // If no constant values, exit.
    if( !num )
    {
        cvalue_list_delete(lst);
        return;
    }

    // Now, sort constant values by "usage gain"
    cvalue_list_sort(lst);

    // Extract all constant values that produce a gain:
    unsigned cs = 0, cn = 0;
    for(unsigned i=0; i<lst->len && nvar<256; i++)
    {
        cvalue *cv = lst->data + i;
        int bytes = cvalue_saved_bytes(cv);
        // Add one extra byte if the variable number is
        // more than 127:
        if( nvar > 127 )
            bytes += cv->count;

        if( bytes > 0 )
            continue;

        // Ok, we can replace the variable
        if( cv->str )
        {
            char name[256];
            sprintf(name, "__s%d", cs);
            cs++;
            nstring++;
            nvar++;
            info_print(expr_get_file_name(prog), 0, "replacing constant var %s$=\"%.*s\" (%d times, %d bytes)\n",
                       name, cv->slen, cv->str, cv->count, bytes);
            // Creates the variable
            cv->vid = vars_new_var(v, name, vtString, expr_get_file_name(prog), 0);
            cv->status = 1;
            // Replace all instances of the constant value with the variables
            replace_cvalue(prog, cv);
        }
        else
        {
            char name[256];
            if( cv->num < 100000 && cv->num == round(cv->num) )
            {
                if( cv->num >= 0 )
                    sprintf(name, "__n%.0f", cv->num);
                else
                    sprintf(name, "__n_%.0f", -cv->num);
            }
            else if( cv->num < 1000 && round(10000000 * cv->num) == 1000000 * round(10 * cv->num) )
            {
                if( cv->num >= 0 )
                    sprintf(name, "__n%.0f_%.0f", trunc(cv->num), 10 * (cv->num - trunc(cv->num)));
                else
                    sprintf(name, "__n_%.0f_%.0f", -trunc(cv->num), -10 * (cv->num - trunc(cv->num)) );
            }
            else
            {
                sprintf(name, "__nd%d", cn);
                cn++;
            }
            nfloat++;
            nvar++;
            info_print(expr_get_file_name(prog), 0, "replacing constant var %s=%g (%d times, %d bytes)\n",
                       name, cv->num, cv->count, bytes);
            cv->vid = vars_new_var(v, name, vtFloat, expr_get_file_name(prog), 0);
            cv->status = 1;
            // Replace all instances of the constant value with the variables
            replace_cvalue(prog, cv);
        }
    }

    // Now, add all variable initializations to the program, first numeric, then strings:
    expr *init = 0, *dim = 0;
    for(unsigned i=0; i<lst->len; i++)
    {
        cvalue *cv = lst->data + i;
        if( cv->status == 1 && !cv->str )
        {
           init = create_num_assign(prog->mngr, lst, init, cv->num, cv->vid);
           cv->status = 2;
        }
    }
    // Now, all DIM expressions
    for(unsigned i=0; i<lst->len; i++)
    {
        cvalue *cv = lst->data + i;
        if( cv->status == 1 && cv->str )
        {
            dim = create_str_dim(prog->mngr, lst, dim, cv->str, cv->slen, cv->vid);
            cv->status = 2;
        }
    }
    if( dim )
        init = expr_new_stmt(prog->mngr, init, dim, STMT_DIM);
    // And all string assignments
    for(unsigned i=0; i<lst->len; i++)
    {
        cvalue *cv = lst->data + i;
        if( cv->status == 2 && cv->str )
        {
            init = create_str_assign(prog->mngr, lst, init, cv->str, cv->slen, cv->vid);
            cv->status = 2;
        }
    }
    add_to_prog(prog, init);

    cvalue_list_delete(lst);
    return;
}

