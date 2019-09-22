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
#include "expr.h"
#include "vars.h"
#include "dbg.h"
#include "parser.h"
#include "program.h"
#include "darray.h"
#include "hash.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Struct to hold constant value and count
typedef struct {
    unsigned count;  // Times repeated
    int vid;         // Variable id assigned
    int status;      // Status: 0 = unused, 1 = variable assigned, 2 = code emitted
    const uint8_t *str; // String value, NULL if not a string
    unsigned slen;   // String length
    double num;      // Value, valid if str == NULL
} cvalue;

// List of cvalue
typedef darray(cvalue) cvalue_list;

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

// Used to store number of bytes needed to encode a value
struct clen {
    double val;
    int bytes;
};

#define CLEN_CACHE_SIZE (1<<14)
typedef struct {
    unsigned len;
    struct clen list[256];
    struct clen cache[CLEN_CACHE_SIZE];
} clen_list;

// TODO: should not be a static variable!
clen_list static_clen_list;

static struct clen *clen_cache_get(clen_list *l, double x)
{
    unsigned pos = hash_any(&x, sizeof(double)) & (CLEN_CACHE_SIZE-1);
    return &l->cache[pos];
}

// Builds a list of the number of bytes needed to encode a value
static void build_clen_list(clen_list *l, cvalue_list *v)
{
    // Clear list
    l->len = 0;
    memset(l->cache, 0, sizeof(struct clen) * CLEN_CACHE_SIZE);

    // Now, add all variables already defined
    for(unsigned i=0; i<v->len && l->len < 256; i++)
    {
        const cvalue *c = v->data + i;
        // Skip strings and variables not already created
        if( c->str || !c->status )
            continue;
        l->list[l->len].val = c->num;
        l->list[l->len].bytes = c->vid > 127 ? 2 : 1;
        struct clen *cl = clen_cache_get(l, c->num);
        *cl = l->list[l->len];
        l->len++;
    }
}

static int exact_div(double x)
{
    return x == 2 || x == 5 || x == 10;
}

static int get_clen_raw(const clen_list *l, double val)
{
    if( !l->len )
        return 7;
    // Try to get val using permutations of current variables
    const struct clen *c1, *c2, *c3;
    // 0: NOT x
    if( val == 0 )
    {
        for( c1 = l->list; c1 < l->list + l->len; c1++)
            if( c1->val != 0 && c1->bytes < 5 )
                return 1 + c1->bytes;
    }
    else if( val == 1 )
    {
        for( c1 = l->list; c1 < l->list + l->len; c1++)
            if( c1->val == 0 && c1->bytes < 5 )
                return 1 + c1->bytes;
    }
    // 1: negated variables
    for( c1 = l->list; c1 < l->list + l->len; c1++)
        if( c1->val != 0 && c1->bytes < 5 )
            if( val == -c1->val )
                return 1 + c1->bytes;
    // 2: Operations on 2 values
    for( c1 = l->list; c1 < l->list + l->len; c1++)
    {
        if( c1->val == 0 || c1->bytes > 4 )
            continue;
        for( c2 = l->list; c2 <= c1; c2++)
        {
            int n = c1->bytes + c2->bytes + 1;
            double x = c1->val, y = c2->val;
            if( y == 0 || n > 6 )
                continue;
            if( val ==  x + y || val == x - y || val == y - x || val == x * y )
                return n;
            if( exact_div(y) && val ==  x / y )
                return n;
            if( exact_div(x) && val ==  y / x )
                return n;
        }
    }
    // 3: Operations on 2 values with "-" first
    for( c1 = l->list; c1 < l->list + l->len; c1++)
    {
        if( c1->val == 0 || c1->bytes > 3 )
            continue;
        for( c2 = l->list; c2 <= c1; c2++)
        {
            int n = c1->bytes + c2->bytes + 2;
            double x = c1->val, y = c2->val;
            if( y == 0 || n > 6 )
                continue;
            if( val ==  - x - y || val == - x * y )
                return n;
            if( exact_div(y) && val ==  - x / y )
                return n;
            if( exact_div(x) && val == - y / x )
                return n;
        }
    }
    // 4: Operations on 3 values
    for( c1 = l->list; c1 < l->list + l->len; c1++)
    {
        if( c1->val == 0 || c1->bytes > 2 )
            continue;
        for( c2 = l->list; c2 <= c1; c2++)
        {
            if( c2->val == 0 || (c1->bytes + c2->bytes) > 3 )
                continue;
            for( c3 = l->list; c3 <= c2; c3++)
            {
                int n = c1->bytes + c2->bytes + c3->bytes + 2;
                double x = c1->val, y = c2->val, z = c3->val;
                if( z == 0 || n > 6 )
                    continue;
                if( val == x + y + z || val == x + y - z || val == x + z - y || val == y + z - x ||
                    val == x - y - z || val == y - x - z || val == z - x - y || val == x * y * z ||
                    val == x * y + z || val == x * z + y || val == y * z + x || val == x * y - z ||
                    val == x * z - y || val == y * z - x || val == x - y * z || val == y - x * z ||
                    val == z - x * y )
                    return n;
                if( exact_div(z) && (val == x + y / z || val == x - y / z || val == y / z - x ||
                    val == y + x / z || val == y - x / z || val == x / z - y) )
                    return n;
                if( exact_div(y) && (val == x + z / y || val == x - z / y || val == z / y - x ||
                    val == z + x / y || val == z - x / y || val == x / y - z) )
                    return n;
                if( exact_div(x) && (val == z + y / x || val == z - y / x || val == y / x - z ||
                    val == y + z / x || val == y - z / x || val == z / x - y) )
                    return n;
            }
        }
    }
#if 1
    // 5: Operations on 3 values with "-" before
    for( c1 = l->list; c1 < l->list + l->len; c1++)
    {
        if( c1->val == 0 || c1->bytes > 2 )
            continue;
        for( c2 = l->list; c2 <= c1; c2++)
        {
            if( c2->val == 0 || (c1->bytes + c2->bytes) > 3 )
                continue;
            for( c3 = l->list; c3 <= c2; c3++)
            {
                int n = c1->bytes + c2->bytes + c3->bytes + 2;
                double x = c1->val, y = c2->val, z = c3->val;
                if( z == 0 || n > 6 )
                    continue;
                if( val == - x - y - z || val == - x * y * z || val == - x - y * z ||
                    val == - y - x * z || val == - z - x * y )
                    return n;
                if( exact_div(z) && (val == - x - y / z || val == - y - x / z) )
                    return n;
                if( exact_div(y) && (val == - x + z / y || val == - z - x / y) )
                    return n;
                if( exact_div(x) && (val == - z + y / x || val == - y - z / x) )
                    return n;
            }
        }
    }
#endif
    return 7;
}

static int get_clen(clen_list *l, double val)
{
    struct clen *c = clen_cache_get(l, val);
    if( c->val != val )
    {
        c->val = val;
        c->bytes = get_clen_raw(l, val);
    }
    return c->bytes;
}

// Function to estimate the number of bytes saved by factorizing this constant
static int cvalue_saved_bytes(const cvalue *c)
{
    if( c->str )
    {
        // Currently, the constant uses 2+LEN bytes each time it appears in the
        // program, by replacing by a variable we use 1 byte each time is used
        // (if variable is < 128), so we save "count * (1+slen)" bytes.
        //
        // To use the constant, we need to add the size of the variable (8+slen
        // bytes), the size of the name (we assume best case, 0 bytes) and the
        // size of the init program:
        //     DIM X$(LEN) : X$="....." :
        // Assuming the c(LEN) is the size of coding LEN, we have:
        //     13 + c(LEN) + slen + 8 + slen
        //
        // Note that if we convert more than one string, the next converted use
        // 2 less bytes, as the "DIM" is reused.
        return (21 + get_clen(&static_clen_list, c->slen) + 2 * c->slen) - c->count * (1+c->slen);
    }
    else
    {
        // Currently, the constant uses 7 bytes each time, it appears in the
        // program, by replacing by a variable we use 1 byte each time is used
        // (if variable is < 128), so we save "count * 6" bytes.
        //
        // To use the constant, we need to add the size of the variable (8
        // bytes), the size of the name (we assume best case, 0 bytes) and the
        // size of the init program, X = (CODE), this is 5 + length of code.
        //
        // Note that when emitting the code, we reuse any already emitted
        // constant value, so the number of bytes could be less.
        return 13 + get_clen(&static_clen_list, c->num) - c->count * 6;
    }
}

// Function to sort constant values based numeric absolute value
// Also, positive numbers come before negative ones and numbers are
// before strings, but the number "0" is at the end.
static int cvalue_sort_abs_comp(const void *pa, const void *pb)
{
    const cvalue *a = pa, *b = pb;
    // If both are number, compare values:
    if( !a->str && !b->str )
    {
        double fa = fabs(a->num), fb = fabs(b->num);
        if( fa == 0 && fb != 0 )
            return 1;
        else if( fb == 0 && fa != 0 )
            return -1;
        if( fa < fb )
            return -1;
        else if( fa > fb )
            return +1;
        else if( a->num > b->num )
            return -1;
        else if( a->num < b->num )
            return +1;
        else
            return 0;
    }
    else if( a->str && b->str )
    {
        if( a->slen < b->slen )
            return -1;
        else if( a->slen > b->slen )
            return +1;
        else
            return 0;
    }
    else if( !a->str )
        return -1;
    else
        return +1;
}

// Function to sort constant values based on bytes saved
static int cvalue_sort_comp(const void *pa, const void *pb)
{
    // Calculate the number of bytes for "a" and "b"
    int sa = cvalue_saved_bytes(pa);
    int sb = cvalue_saved_bytes(pb);
    if( sa != sb )
        return sa-sb;
    else
        return cvalue_sort_abs_comp(pa, pb);
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

static void cvalue_list_sort_abs(cvalue_list *l)
{
    qsort(l->data, l->len, sizeof(l->data[0]), cvalue_sort_abs_comp);
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
        darray_add(l, val);
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

static expr *expr_from_vid(expr_mngr *m, int vid)
{
    if( vid < 0 )
        return expr_new_tok(m, vid & 0xFF);
    else
        return expr_new_var_num(m, vid);
}

static expr *create_num(expr_mngr *m, const cvalue_list *l, double n)
{
    // Creates the optimal numeric initialization expression, using
    // already initialized variable.
    unsigned vnum = 0;
    double val[256]; // Max 256 values
    int vid[256];

    // Create a list of available values - makes the following code faster
    for(unsigned i=0; i<l->len && vnum < 256; i++)
    {
        const cvalue *c = l->data + i;
        // Skip strings and variables not already created
        if( c->str || c->status < 2 )
            continue;
        vid[vnum] = c->vid;
        val[vnum] = c->num;
        vnum++;
    }

    // First, try value already in the table:
    for(unsigned i=0; i < vnum; i++)
        if( n == val[i] )
            return expr_from_vid(m, vid[i]);

    // Special cases for "0" and "1": NOT x
    if( n == 0 )
        for(unsigned i=0; i < vnum; i++)
            if( val[i] != 0 )
                return expr_new_uni(m, expr_from_vid(m, vid[i]), TOK_NOT);
    if( n == 1 )
        for(unsigned i=0; i < vnum; i++)
            if( val[i] == 0 )
                return expr_new_uni(m, expr_from_vid(m, vid[i]), TOK_NOT);

    // Now, try with "MINUS" and a value:
    for(unsigned i=0; i < vnum; i++)
        if( n == -val[i] )
            return expr_new_uni(m, expr_from_vid(m, vid[i]), TOK_UMINUS);

    // Now, try with operations between two simple values:
    for(unsigned i=0; i < vnum; i++)
        for(unsigned j=0; j < vnum; j++)
        {
            double x = val[i], y = val[j];
            int vi = vid[i], vj = vid[j];
            if( n == x + y )
                return expr_new_bin(m, expr_from_vid(m, vi), expr_from_vid(m, vj), TOK_PLUS);
            if( n == x - y )
                return expr_new_bin(m, expr_from_vid(m, vi), expr_from_vid(m, vj), TOK_MINUS);
            if( n == x * y )
                return expr_new_bin(m, expr_from_vid(m, vi), expr_from_vid(m, vj), TOK_STAR);
            if( n == x / y )
                return expr_new_bin(m, expr_from_vid(m, vi), expr_from_vid(m, vj), TOK_SLASH);
        }

    // Same as before, adding "-":
    for(unsigned i=0; i < vnum; i++)
        for(unsigned j=0; j < vnum; j++)
        {
            double x = val[i], y = val[j];
            int vi = vid[i], vj = vid[j];
            if( n == -x - y )
                return expr_new_bin(m, expr_new_uni(m, expr_from_vid(m, vi), TOK_UMINUS), expr_from_vid(m, vj), TOK_MINUS);
            if( n == -x * y )
                return expr_new_bin(m, expr_new_uni(m, expr_from_vid(m, vi), TOK_UMINUS), expr_from_vid(m, vj), TOK_STAR);
            if( n == -x / y )
                return expr_new_bin(m, expr_new_uni(m, expr_from_vid(m, vi), TOK_UMINUS), expr_from_vid(m, vj), TOK_SLASH);
        }

    // Now, try with operations between tree simple values:
    for(unsigned i=0; i < vnum; i++)
        for(unsigned j=0; j < vnum; j++)
            for(unsigned k=0; k < vnum; k++)
            {
                double x = val[i], y = val[j], z = val[k];
                int vi = vid[i], vj = vid[j], vk = vid[k];
                if( n == x + y + z )
                    return expr_new_bin(m, expr_new_bin(m, expr_from_vid(m,vi), expr_from_vid(m,vj), TOK_PLUS), expr_from_vid(m,vk), TOK_PLUS);
                if( n == x - y + z )
                    return expr_new_bin(m, expr_new_bin(m, expr_from_vid(m,vi), expr_from_vid(m,vj), TOK_MINUS), expr_from_vid(m,vk), TOK_PLUS);
                if( n == x - y - z )
                    return expr_new_bin(m, expr_new_bin(m, expr_from_vid(m,vi), expr_from_vid(m,vj), TOK_MINUS), expr_from_vid(m,vk), TOK_MINUS);
                if( n == x * y + z)
                    return expr_new_bin(m, expr_new_bin(m, expr_from_vid(m,vi), expr_from_vid(m,vj), TOK_STAR), expr_from_vid(m,vk), TOK_PLUS);
                if( n == x * y - z)
                    return expr_new_bin(m, expr_new_bin(m, expr_from_vid(m,vi), expr_from_vid(m,vj), TOK_STAR), expr_from_vid(m,vk), TOK_MINUS);
                if( n == x - y * z)
                    return expr_new_bin(m, expr_from_vid(m,vi), expr_new_bin(m, expr_from_vid(m,vj), expr_from_vid(m,vk), TOK_STAR), TOK_MINUS);
                if( n == x * y * z)
                    return expr_new_bin(m, expr_new_bin(m, expr_from_vid(m,vi), expr_from_vid(m,vj), TOK_STAR), expr_from_vid(m,vk), TOK_STAR);
                if( n == x / y + z )
                    return expr_new_bin(m, expr_new_bin(m, expr_from_vid(m,vi), expr_from_vid(m,vj), TOK_SLASH), expr_from_vid(m,vk), TOK_PLUS);
                if( n == x / y - z )
                    return expr_new_bin(m, expr_new_bin(m, expr_from_vid(m,vi), expr_from_vid(m,vj), TOK_SLASH), expr_from_vid(m,vk), TOK_MINUS);
                if( n == x - y / z)
                    return expr_new_bin(m, expr_from_vid(m,vi), expr_new_bin(m, expr_from_vid(m,vj), expr_from_vid(m,vk), TOK_SLASH), TOK_MINUS);
            }

    // Same as before, adding "-":
    for(unsigned i=0; i < vnum; i++)
        for(unsigned j=0; j < vnum; j++)
            for(unsigned k=0; k < vnum; k++)
            {
                double x = val[i], y = val[j], z = val[k];
                int vi = vid[i], vj = vid[j], vk = vid[k];
                if( n == -x - y - z )
                    return expr_new_bin(m, expr_new_bin(m, expr_new_uni(m, expr_from_vid(m,vi), TOK_UMINUS), expr_from_vid(m,vj), TOK_MINUS), expr_from_vid(m,vk), TOK_MINUS);
                if( n == -x * y - z)
                    return expr_new_bin(m, expr_new_bin(m, expr_new_uni(m, expr_from_vid(m,vi), TOK_UMINUS), expr_from_vid(m,vj), TOK_STAR), expr_from_vid(m,vk), TOK_MINUS);
                if( n == -x * y * z)
                    return expr_new_bin(m, expr_new_bin(m, expr_new_uni(m, expr_from_vid(m,vi), TOK_UMINUS), expr_from_vid(m,vj), TOK_STAR), expr_from_vid(m,vk), TOK_STAR);
                if( n == -x / y - z )
                    return expr_new_bin(m, expr_new_bin(m, expr_new_uni(m, expr_from_vid(m,vi), TOK_UMINUS), expr_from_vid(m,vj), TOK_SLASH), expr_from_vid(m,vk), TOK_MINUS);
            }

    // No simpler expression found:
    return expr_new_number(m, n);
}

static expr *create_num_assign(expr_mngr *m, cvalue_list *l, expr *prev, double x, int vid)
{
    expr *toks = expr_new_bin(m, expr_new_var_num(m, vid), create_num(m, l, x), TOK_F_ASGN);
    return expr_new_stmt(m, prev, toks, STMT_LET_INV);
}

static expr *create_str_dim(expr_mngr *m, cvalue_list *l, expr *exp, unsigned len, int vid)
{
    // Create the DIM expression:  [,] X$(len)
    expr *dim = expr_new_bin(m, expr_new_var_str(m, vid), create_num(m, l, len), TOK_DS_L_PRN);
    if( exp )
        return expr_new_bin(m, exp, dim, TOK_COMMA);
    else
        return dim;
}

static expr *create_str_assign(expr_mngr *m, expr *prev, const uint8_t *data, unsigned len, int vid)
{
    // Create the assign statement: X$=""
    return expr_new_stmt(m, prev, expr_new_bin(m, expr_new_var_str(m, vid), expr_new_string(m, data, len), TOK_S_ASGN), STMT_LET_INV);
}

static void add_to_prog(expr *prog, expr *e)
{
    if( !e )
        return;

    // Swap prog with e
    expr tmp;
    tmp = *e;
    *e = *prog;
    *prog = tmp;

    // Link
    while(prog->lft)
        prog = prog->lft;
    prog->lft = e;
}

int opt_replace_const(expr *prog)
{
    if( !prog )
        return 0;

    // Get current number of variables, used to estimate the
    // memory usage of each new constant added to the program
    vars *v = pgm_get_vars( expr_get_program(prog) );
    unsigned nfloat  = vars_get_count(v, vtFloat);
    unsigned nstring = vars_get_count(v, vtString);
    unsigned nvar    = vars_get_total(v);

    // If not enough variables, exit
    int max_vars = (parser_get_dialect() == parser_dialect_turbo) ? 256 : 128;

    if( nvar >= max_vars )
        return 0;

    // Initialize list of constant values
    cvalue_list *lst = darray_new(cvalue,256);

    // Adds TurboBasic XL integrated constants: %0 to %3
    if(parser_get_dialect() == parser_dialect_turbo)
    {
        cvalue val;
        memset(&val, 0, sizeof(val));
        val.count = 1;
        val.status = 2;
        val.num = 0;
        val.vid = TOK_PER_0 - 256;
        darray_add(lst, val);
        val.num = 1;
        val.vid = TOK_PER_1 - 256;
        darray_add(lst, val);
        val.num = 2;
        val.vid = TOK_PER_2 - 256;
        darray_add(lst, val);
        val.num = 3;
        val.vid = TOK_PER_3 - 256;
        darray_add(lst, val);
    }

    // Initialize list of gains for each possible value
    build_clen_list(&static_clen_list, lst);

    // Search all constant values in the program and store
    // the value and number of times repeated
    int num = update_cvalue(prog, lst);

    // If no constant values, exit.
    if( !num )
    {
        darray_free(lst);
        return 0;
    }

    // Now, sort constant values by "usage gain", to try to convert better constants first
    cvalue_list_sort(lst);


    // Redo selection until no more gains are possible
    int retry = 1;
    while( retry )
    {
        retry = 0;

        // Extract all constant values that produce a gain:
        unsigned cs = 0, cn = 0;
        for(unsigned i=0; i<lst->len && nvar<max_vars; i++)
        {
            cvalue *cv = lst->data + i;

            // Skip if already done
            if( cv->status )
                continue;

            int bytes = cvalue_saved_bytes(cv);
            // Add one extra byte if the variable number is more than 127:
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
                // Rebuild cost list and retry
                build_clen_list(&static_clen_list, lst);
                retry = 1;
                break;
            }
        }
    }

    // Sort again by absolute value, this tends to generate smaller code
    cvalue_list_sort_abs(lst);

    // Now, add all variable initializations to the program, first numeric, then strings:
    expr *init = 0, *last_stmt = 0, *dim = 0;
    for(unsigned i=0; i<lst->len; i++)
    {
        cvalue *cv = lst->data + i;
        if( cv->status == 1 && !cv->str )
        {
           last_stmt = create_num_assign(prog->mngr, lst, last_stmt, cv->num, cv->vid);
           if( !init ) init = last_stmt;
           cv->status = 2;
        }
    }
    // Now, all DIM expressions
    for(unsigned i=0; i<lst->len; i++)
    {
        cvalue *cv = lst->data + i;
        if( cv->status == 1 && cv->str )
        {
            dim = create_str_dim(prog->mngr, lst, dim, cv->slen, cv->vid);
            cv->status = 2;
        }
    }
    if( dim )
    {
        last_stmt = expr_new_stmt(prog->mngr, last_stmt, dim, STMT_DIM);
        if( !init ) init = last_stmt;
    }
    // And all string assignments
    for(unsigned i=0; i<lst->len; i++)
    {
        cvalue *cv = lst->data + i;
        if( cv->status == 2 && cv->str )
        {
            last_stmt = create_str_assign(prog->mngr, last_stmt, cv->str, cv->slen, cv->vid);
            if( !init ) init = last_stmt;
            cv->status = 2;
        }
    }

    add_to_prog(prog, init);

    darray_free(lst);
    return 0;
}

