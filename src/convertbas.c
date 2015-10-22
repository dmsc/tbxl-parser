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

#include "convertbas.h"
#include "expr.h"
#include "vars.h"
#include "program.h"
#include "dbg.h"
#include "defs.h"
#include <assert.h>
#include <stdlib.h>
#include <math.h>

// Simplify printing warnings
#define warn(...) \
    do { warn_print(expr_get_file_name(ex), expr_get_file_line(ex), __VA_ARGS__); } while(0)
#define error(...) \
    do { err_print(expr_get_file_name(ex), expr_get_file_line(ex), __VA_ARGS__); } while(0)

static void memory_error(void)
{
    fprintf(stderr,"INTERNAL ERROR: memory allocation failure.\n");
    abort();
}

// TODO: move to header - based on "dlist.h" on CCAN
#define darray(type)     struct {type *data; size_t len; size_t size;}
static void darray_fill(void *arr, size_t sz, size_t init)
{
    darray(char) *ret = arr;
    if( !ret || !(ret->data = malloc(sz * init)) )
        memory_error();
    ret->len = 0;
    ret->size = init;
}

static void *darray_alloc(size_t sz, size_t init)
{
    darray(char) *ret = malloc(sizeof(darray(char)));
    darray_fill(ret, sz, init);
    return ret;
}

static void darray_grow(void *arr, size_t sz, size_t newsize)
{
    darray(char) *p = arr;
    while( newsize > p->size )
    {
        p->size *= 2;
        if( !p->size || !(p->data = realloc(p->data, sz * p->size)) )
            memory_error();
    }
}

static void darray_free(void *arr)
{
    darray(char) *p = arr;
    free(p->data);
    free(p);
}

static void darray_delete(void *arr)
{
    darray(char) *p = arr;
    free(p->data);
}

#define darray_init(arr, init_size) darray_fill(arr, sizeof((arr)->data[0]), init_size)
#define darray_new(type, init_size) darray_alloc(sizeof(type), init_size)
#define darray_add(arr, val) do { \
    darray_grow((arr),sizeof((arr)->data[0]), (arr)->len+1); \
    (arr)->data[(arr)->len] = val; \
    (arr)->len ++; \
} while(0)
#define darray_len(arr) ((arr)->len)
#define darray_i(arr,i) ((arr)->data[i])

// Store a PROC parameter
typedef struct {
    const char *name; // Parameter name
    int var;          // Original variable ID
    int new_var;      // New variable ID
    int sdim;         // String dimension, or 0 if number
    int local;        // 1 = local var, 0 = parameter
} param;
typedef darray(param) param_list;

// Store a PROC info
typedef struct {
    const char *name; // proc name
    int label;        // label id
    int num_args;     // number of arguments
    param_list params;// list of parameters
} proc;
typedef darray(proc) proc_list;

// Store a new proc
static void proc_init(proc *p, const char *name, int label)
{
    p->name = name;
    p->label = label;
    p->num_args = 0;
    darray_init(&p->params, 16);
}

// Swap variables inside proc with local replacements
static void do_swap_vars(expr *ex, param_list *pl)
{
    if( !ex )
        return;

    do_swap_vars(ex->lft, pl);
    do_swap_vars(ex->rgt, pl);

    if( ex->type == et_var_number || ex->type == et_var_string )
    {
        size_t i;
        for(i=0; i<darray_len(pl); i++)
        {
            if( ex->var == darray_i(pl, i).var )
                ex->var = darray_i(pl, i).new_var;
        }
    }
}

// Gets the integer numeric value of a node, or -1 if error
static double get_numeric_val(expr *ex)
{
    assert(ex);
    if( ex->type == et_c_number || ex->type == et_c_hexnumber )
        return ex->num;
    else if( ex->type == et_tok )
    {
        if( ex->tok == TOK_PER_0 )
            return 0;
        else if( ex->tok == TOK_PER_1 )
            return 1;
        else if( ex->tok == TOK_PER_2 )
            return 2;
        else if( ex->tok == TOK_PER_3 )
            return 3;
    }
    else if( ex->type == et_def_number )
    {
        const defs *d = pgm_get_defs(expr_mngr_get_program(ex->mngr));
        return defs_get_numeric(d, ex->var);
    }
    return -1;
}

// Adds all variables in "ex" to the proc parameter list.
// Returns != 0 on error
static int add_proc_args(proc *pc, expr *ex, int local)
{
    if( !ex )
        return 0;
    assert(ex->type == et_tok || ex->type == et_var_string || ex->type == et_var_number);
    if( ex->type == et_tok && ex->tok == TOK_COMMA )
    {
        int err = add_proc_args(pc, ex->rgt, local);
        return err | add_proc_args(pc, ex->lft, local);
    }
    else if( ex->type == et_var_number || (ex->type == et_tok && ex->tok == TOK_DS_L_PRN) )
    {
        vars *vl = pgm_get_vars(expr_mngr_get_program(ex->mngr));
        param p;
        if( ex->type == et_tok )
        {
            // String variable
            assert(ex->lft && ex->lft->type == et_var_string);
            assert(ex->rgt);
            p.var = ex->lft->var;
            double dnum = get_numeric_val(ex->rgt);
            if( dnum < 1 || dnum > 65535 || floor(dnum) != dnum )
            {
                error("string dimension should be an integer > 1 and < 65535\n");
                return 1;
            }
            p.sdim = (int)dnum;
        }
        else
        {
            p.var  = ex->var;
            p.sdim = 0;
        }
        p.name = vars_get_long_name(vl, p.var);
        p.local = local;
        // Add new "local" variable/arg
        char buf[144];
        sprintf(buf,"_%s_%.64s_%.64s", local?"local":"param", pc->name, p.name);
        p.new_var = vars_new_var(vl, buf, p.sdim ? vtString : vtFloat,
                                 expr_get_file_name(ex), expr_get_file_line(ex));
        darray_add(&pc->params, p);
        if( !local )
            pc->num_args ++;
        return 0;
    }
    else
    {
        error("invalid argument/local in PROC\n");
        return 1;
    }
}

// Search PROCs and store name and parameters in the proc list
// Returns != 0 on error
static int do_search_procs(expr *ex, proc_list *pl)
{
    int err = 0;
    // Stores currently processed PROC
    proc *inproc = 0;

    // Process all statements
    for( ; ex ; ex = ex->lft )
    {
        // Only process statements
        if( ex->type == et_lnum )
            continue;

        assert(ex->type == et_stmt);

        if( ex->stmt == STMT_PROC_VAR || ex->stmt == STMT_PROC )
        {
            // Process this PROC
            if( inproc )
                warn("new PROC inside PROC '%s', expect problems.\n", inproc->name);
            expr *lbl, *args, *locals;
            if( ex->stmt == STMT_PROC )
            {
                lbl = ex->rgt;
                args = 0;
                locals = 0;
            }
            else
            {
                assert( ex->rgt && ex->rgt->type == et_tok && ex->rgt->tok == TOK_COMMA );
                lbl = ex->rgt->lft;
                assert( ex->rgt->rgt && ex->rgt->rgt->type == et_tok && ex->rgt->rgt->tok == TOK_SEMICOLON );
                args = ex->rgt->rgt->lft;
                locals = ex->rgt->rgt->rgt;
            }
            assert( lbl && lbl->type == et_var_label );
            // Add to PROC list
            vars *vl = pgm_get_vars(expr_mngr_get_program(ex->mngr));
            proc nproc;
            proc_init(&nproc, vars_get_long_name(vl, lbl->var), lbl->var);
            // Add all arguments
            err |= add_proc_args(&nproc, args, 0);
            err |= add_proc_args(&nproc, locals, 1);
            // Store in PROC list
            darray_add(pl, nproc);
            // Store pointer in "inproc"
            inproc = & darray_i(pl, darray_len(pl)-1);
            // Convert this PROC_VAR to a PROC
            ex->stmt = STMT_PROC;
            ex->rgt  = lbl;
        }
        else if( ex->stmt == STMT_ENDPROC )
        {
            // End current proc
            if( !inproc )
                warn("ENDPROC without PROC.\n");
            else
            {
                inproc = 0;
            }
        }
        else if( inproc )
        {
            // TODO: Detect recursive calls
            // Inside PROC, swap all variable references in the proc variable list
            do_swap_vars(ex->rgt, &inproc->params );
        }
    }
    return err;
}

// Return the number of parameters (arguments) on an EXEC call
static int count_exec_params(expr *ex)
{
    if( !ex )
        return 0;
    if( ex->type == et_tok && ex->tok == TOK_COMMA )
        return count_exec_params(ex->rgt) + count_exec_params(ex->lft);
    return 1;
}

// Set EXEC parameters. Returns != 0 on error.
static int set_exec_params(proc *pc, expr *ex, expr *cur_stmt, size_t n)
{
    if( !ex )
        return 0;

    if( ex->type == et_tok && ex->tok == TOK_COMMA )
    {
        int err = set_exec_params(pc, ex->rgt, cur_stmt, n);
        return err | set_exec_params(pc, ex->lft, cur_stmt, n + 1);
    }

    if( n >= pc->num_args )
        return 1;

    // Create an assignment statement....
    expr_mngr *mngr = ex->mngr;
    param *p = &darray_i(&pc->params,n);
    expr *toks;
    if( p->sdim )
        toks = expr_new_bin(mngr, expr_new_var_str(mngr, p->new_var), ex, TOK_S_ASGN);
    else
        toks = expr_new_bin(mngr, expr_new_var_num(mngr, p->new_var), ex, TOK_F_ASGN);

    // Search statement *BEFORE* our stmm
    expr *st = pgm_get_expr(expr_mngr_get_program(mngr));
    while( st && st->lft != cur_stmt )
        st = st->lft;
    // Add to statements
    expr *stmt = expr_new_stmt(mngr, st, toks, STMT_LET_INV);
    stmt->lft = cur_stmt;
    return 0;
}

// Search EXEC_PAR and generate code to assign parameters
// returns != 0 on error.
static int do_search_exec(expr *ex, proc_list *pl)
{
    int err = 0;
    // Process all statements
    for( ; ex ; ex = ex->lft )
    {
        // Only process statements
        if( ex->type == et_lnum )
            continue;
        assert(ex->type == et_stmt);

        if( ex->stmt == STMT_EXEC_PAR || ex->stmt == STMT_EXEC )
        {
            // TODO: Check type of arguments

            // For each parameter, generate code to assign parameter
            expr *label;
            expr *params;
            if( ex->stmt == STMT_EXEC_PAR )
            {
                assert(ex->rgt && ex->rgt->type == et_tok && ex->rgt->tok == TOK_COMMA);
                assert(ex->rgt->lft->type == et_var_label);
                label = ex->rgt->lft;
                params = ex->rgt->rgt;
            }
            else
            {
                assert(ex->rgt && ex->rgt->type == et_var_label);
                label = ex->rgt;
                params = 0;
            }
            vars *vl = pgm_get_vars(expr_mngr_get_program(ex->mngr));
            proc *pc = 0;
            for(size_t i=0; i<darray_len(pl); i++)
            {
                pc = &darray_i(pl,i);
                if( pc->label == label->var )
                    break;
            }
            if( !pc )
            {
                error("EXEC to missing PROC '%s'\n", vars_get_long_name(vl, label->var));
                err = 1;
                continue;
            }
            size_t num = count_exec_params(params);
            if( num != pc->num_args )
            {
                error("EXEC with too %s parameters to PROC '%s'\n",
                      num<pc->num_args ? "few" : "many",
                      vars_get_long_name(vl, label->var));
                err = 1;
                continue;
            }

            err |= set_exec_params(pc, params, ex, 0);
            // Convert to EXEC
            ex->stmt = STMT_EXEC;
            ex->rgt = label;
        }
    }
    return err;
}

static expr *create_num(expr_mngr *m, int num)
{
    if( num == 1 )
        return expr_new_tok(m,TOK_PER_1);
    else if( num == 2 )
        return expr_new_tok(m,TOK_PER_2);
    else if( num == 3 )
        return expr_new_tok(m,TOK_PER_3);
    else
        return expr_new_number(m,num);
}

static expr *create_str_dim(expr_mngr *m, expr *exp, unsigned len, int var)
{
    // Create the DIM expression:  [,] X$(len)
    expr *dim = expr_new_bin(m, expr_new_var_str(m, var), create_num(m, len), TOK_DS_L_PRN);
    if( exp )
        return expr_new_bin(m, exp, dim, TOK_COMMA);
    else
        return dim;
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

static int do_add_dims(expr *ex, proc_list *pl)
{
    int ndim = 0;
    expr *dim = 0, *dims = 0;
    for(size_t i=0; i<darray_len(pl); i++)
    {
        proc *pc = &darray_i(pl, i);
        for(size_t j=0; j<darray_len(&pc->params); j++)
        {
            param *p = &darray_i(&pc->params, j);
            if( p->sdim )
            {
                dim = create_str_dim(ex->mngr, dim, p->sdim, p->new_var);
                ndim ++;
            }
            if( ndim > 14 )
            {
                // Finalize this DIM and add a new one
                expr * dim_stmt = expr_new_stmt(ex->mngr, 0, dim, STMT_DIM);
                dim_stmt->lft = dims;
                dims = dim_stmt;
                dim = 0;
                ndim = 0;
            }
        }
    }
    if( dim )
    {
        expr * dim_stmt = expr_new_stmt(ex->mngr, 0, dim, STMT_DIM);
        dim_stmt->lft = dims;
        dims = dim_stmt;
    }
    add_to_prog(ex, dims);
    return 0;
}

static int convert_proc_exec(expr *ex)
{
    int err = 0;
    // Make a list of all "PROC_VAR" and store the parameter list
    proc_list *plist = darray_new(proc_list,16);
    err |= do_search_procs(ex, plist);

    // Convert EXECs
    err |= do_search_exec(ex, plist);

    // Add DIMs at the start of the program
    err |= do_add_dims(ex, plist);

    // Free memory
    for(size_t i=0; i<darray_len(plist); i++)
        darray_delete( &darray_i(plist,i).params );
    darray_free( plist );

    return err;
}

int convert_to_turbobas(program *p)
{
    int err = 0;
    expr *ex = pgm_get_expr(p);

    // Converts PROC/EXEC with parameters and local variables to standard PROC/EXEC.
    err |= convert_proc_exec(ex);

    return err;
}
