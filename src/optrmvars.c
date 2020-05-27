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
    int replace;       // Will replace with constant value
    double rep_val;    // Value to replace
    int rep_line;      // Line number of assignment
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

// Check if an expr is an assignment
static int tok_is_assignment(expr *ex)
{
    if( ex->type != et_tok )
        return 0;

    // Considers also DIM as assignments
    return ex->tok == TOK_F_ASGN || ex->tok == TOK_S_ASGN ||
           ex->tok == TOK_D_L_PRN || ex->tok == TOK_DS_L_PRN;
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

static int write_var(expr *ex, var_list *vl);

// Checks if expr is a constant number
static int expr_is_cnum(expr *ex)
{
    return ex && (ex->type == et_c_number || ex->type == et_c_hexnumber);
}

// Counts vars inside an expression
static int read_expr(expr *ex, var_list *vl, int in_for_stmt)
{
    if( !ex )
        return 0;

    // Check if this is a variable (being read)
    if( expr_is_var(ex) )
    {
        assert(ex->var>=0 && ex->var<darray_len(vl));
        darray_i(vl, ex->var).read ++;
    }

    int err = 0;
    if( tok_is_assignment(ex) )
    {
        assert(ex->lft && ex->rgt);
        // Assignments, process variables as written at left side
        err = write_var(ex->lft, vl);
        // Check if we are assigning a constant value to a float variable, but
        // ignore FOR as it assigns multiple times.
        if( !in_for_stmt && ex->lft->type == et_var_number )
        {
            assert(ex->lft->var>=0 && ex->lft->var<darray_len(vl));
            var_usage *vu = &darray_i(vl, ex->lft->var);
            // Check right expression:
            // Constant value?
            if( expr_is_cnum(ex->rgt) )
            {
                vu->rep_val = ex->rgt->num;
                vu->rep_line = expr_get_file_line(ex);
            }
            // Negative of constant value
            else if( ex->rgt->type == et_tok && ex->rgt->tok == TOK_UMINUS )
            {
                assert(ex->rgt->rgt);
                if( expr_is_cnum(ex->rgt->rgt) )
                {
                    vu->rep_val = - ex->rgt->rgt->num;
                    vu->rep_line = expr_get_file_line(ex);
                }
            }
            // Sum of two constant values?
            else if( ex->rgt->type == et_tok && ex->rgt->tok == TOK_PLUS )
            {
                assert(ex->rgt->rgt && ex->rgt->lft);
                if( expr_is_cnum(ex->rgt->rgt) && expr_is_cnum(ex->rgt->lft) )
                {
                    vu->rep_val = ex->rgt->lft->num + ex->rgt->rgt->num;
                    vu->rep_line = expr_get_file_line(ex);
                }
            }
            // Sum of two constant values?
            else if( ex->rgt->type == et_tok && ex->rgt->tok == TOK_MINUS )
            {
                assert(ex->rgt->rgt && ex->rgt->lft);
                if( expr_is_cnum(ex->rgt->rgt) && expr_is_cnum(ex->rgt->lft) )
                {
                    vu->rep_val = ex->rgt->lft->num - ex->rgt->rgt->num;
                    vu->rep_line = expr_get_file_line(ex);
                }
            }
            // Multiplication of two constant values?
            else if( ex->rgt->type == et_tok && ex->rgt->tok == TOK_STAR )
            {
                assert(ex->rgt->rgt && ex->rgt->lft);
                if( expr_is_cnum(ex->rgt->rgt) && expr_is_cnum(ex->rgt->lft) )
                {
                    vu->rep_val = ex->rgt->lft->num * ex->rgt->rgt->num;
                    vu->rep_line = expr_get_file_line(ex);
                }
            }
        }
    }
    else
        err = read_expr(ex->lft, vl, in_for_stmt);
    return err | read_expr(ex->rgt, vl, in_for_stmt);
}

// Counts a var as written to
static int write_var(expr *ex, var_list *vl)
{
    assert(ex);
    if( expr_is_var(ex) )
    {
        assert(ex->var>=0 && ex->var<darray_len(vl));
        darray_i(vl, ex->var).written ++;
        return 0;
    }
    if( ex->type == et_tok && (ex->tok == TOK_A_L_PRN || ex->tok == TOK_S_L_PRN) )
    {
        int err = write_var(ex->lft, vl);
        return err | read_expr(ex->rgt, vl, 0);
    }
    return 1;
}

// Counts a list of variables as written to
static int write_var_list(expr *ex, var_list *vl)
{
    if( !ex )
        return 0;

    assert(ex->type == et_tok || expr_is_var(ex));

    if( ex->type == et_tok && ex->tok == TOK_COMMA )
    {
        int err = write_var_list(ex->rgt, vl);
        return err | write_var_list(ex->lft, vl);
    }
    else
        return write_var(ex, vl);
}

// Process: expr , var [, var ...]
static int expr_comma_var_list(expr *ex, var_list *vl)
{
    if( !ex || ex->type != et_tok )
        return 1;
    if( ex->tok != TOK_COMMA && ex->tok != TOK_SEMICOLON )
        return 1;
    int err = read_expr(ex->lft, vl, 0);
    return err | write_var_list(ex->rgt, vl);
}

// Process: expr , expr, var [, var ...]
static int expr_comma2_var_list(expr *ex, var_list *vl)
{
    if( !ex || ex->type != et_tok || ex->tok != TOK_COMMA )
        return 1;
    int err = read_expr(ex->lft, vl, 0);
    return err | expr_comma_var_list(ex->rgt, vl);
}

// Counts usage of variable inside statement
static int stmt_var_usage(expr *ex, var_list *vl)
{
    assert(ex && ex->type == et_stmt);

    switch(ex->stmt)
    {
        // Don't use variables
        case STMT_BAS_ERROR:
        case STMT_DATA:
        case STMT_REM:
        case STMT_REM_:
        case STMT_REM_HIDDEN:
            return 0;

        case STMT_CLR:
            // Can't know the outcome, warn!
            warn_print(expr_get_file_name(ex), expr_get_file_line(ex),
                       "CLR reset variable values, "
                       "can cause problems with variable replacement.");
            return 0;
        case STMT_ENTER:
            warn_print(expr_get_file_name(ex), expr_get_file_line(ex),
                       "ENTER can add new variables and change values, "
                       "causing problems with variable replacement.");
            return 0;

        case STMT_LBL_S:
            return write_var(ex->rgt, vl);

        // Statements with optional I/O channel
        case STMT_GET:
        case STMT_P_GET:
        case STMT_INPUT:
            assert(ex->rgt);
            if( expr_is_var(ex->rgt) || (ex->lft && expr_is_var(ex->rgt->lft)) )
                return write_var_list(ex->rgt, vl);
            else
                return expr_comma_var_list(ex->rgt, vl);

        // Statement with 2 expressions before variable
        case STMT_LOCATE:
            return expr_comma2_var_list(ex->rgt, vl);

        // Statements with 1 expression before variable
        case STMT_NOTE:
        case STMT_STATUS:
            return expr_comma_var_list(ex->rgt, vl);

        case STMT_NEXT:
        case STMT_PROC:
        case STMT_PROC_VAR:
        case STMT_READ:
            return write_var_list(ex->rgt, vl);

        // Handles FOR, we don't want to remove a variable
        // used only in a "FOR", so we mark it as written twice
        case STMT_FOR:
            return read_expr(ex->rgt, vl, 1);

        // Assignments, can be handle by read_expr because
        // use special tokens
        case STMT_COM:
        case STMT_DIM:
        case STMT_LET:
        case STMT_LET_INV:
        // Only read from variables
        case STMT_BGET:
        case STMT_BLOAD:
        case STMT_BPUT:
        case STMT_BRUN:
        case STMT_BYE:
        case STMT_CIRCLE:
        case STMT_CLOAD:
        case STMT_CLOSE:
        case STMT_CLS:
        case STMT_COLOR:
        case STMT_CONT:
        case STMT_CSAVE:
        case STMT_DEG:
        case STMT_DEL:
        case STMT_DELETE:
        case STMT_DIR:
        case STMT_DO:
        case STMT_DOS:
        case STMT_DPOKE:
        case STMT_DRAWTO:
        case STMT_DSOUND:
        case STMT_DUMP:
        case STMT_ELSE:
        case STMT_END:
        case STMT_ENDIF:
        case STMT_ENDIF_INVISIBLE:
        case STMT_ENDPROC:
        case STMT_EXEC:
        case STMT_EXEC_PAR:
        case STMT_EXIT:
        case STMT_FCOLOR:
        case STMT_FILLTO:
        case STMT_F_B:
        case STMT_F_F:
        case STMT_F_L:
        case STMT_GOSUB:
        case STMT_GOTO:
        case STMT_GO_S:
        case STMT_GO_TO:
        case STMT_GRAPHICS:
        case STMT_IF:
        case STMT_IF_MULTILINE:
        case STMT_IF_NUMBER:
        case STMT_IF_THEN:
        case STMT_LIST:
        case STMT_LOAD:
        case STMT_LOCK:
        case STMT_LOOP:
        case STMT_LPRINT:
        case STMT_MOVE:
        case STMT_NEW:
        case STMT_N_MOVE:
        case STMT_ON:
        case STMT_OPEN:
        case STMT_PAINT:
        case STMT_PAUSE:
        case STMT_PLOT:
        case STMT_POINT:
        case STMT_POKE:
        case STMT_POP:
        case STMT_POSITION:
        case STMT_PRINT:
        case STMT_PRINT_:
        case STMT_PUT:
        case STMT_P_PUT:
        case STMT_RAD:
        case STMT_RENAME:
        case STMT_RENUM:
        case STMT_REPEAT:
        case STMT_RESTORE:
        case STMT_RETURN:
        case STMT_RUN:
        case STMT_SAVE:
        case STMT_SETCOLOR:
        case STMT_SOUND:
        case STMT_STOP:
        case STMT_TEXT:
        case STMT_TIME_S:
        case STMT_TRACE:
        case STMT_TRAP:
        case STMT_UNLOCK:
        case STMT_UNTIL:
        case STMT_WEND:
        case STMT_WHILE:
        case STMT_XIO:
            return read_expr(ex->rgt, vl, 0);
    }
    return 1;
}

static int do_detail_var_usage(expr *ex, var_list *vl)
{
    int err = 0;
    // Process all statements
    for( ; ex ; ex = ex->lft )
        if( ex->type == et_stmt )
            err |= stmt_var_usage(ex, vl);
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

// Replace variable assignment with a REM
static int do_replace_var_assign(expr *ex, int id, double val)
{
    int rep = 0;
    for( ; ex ; ex = ex->lft )
        if( ex->type == et_stmt && ( ex->stmt == STMT_LET || ex->stmt == STMT_LET_INV ) )
        {
            if( ex->rgt && ex->rgt->type == et_tok && ex->rgt->tok == TOK_F_ASGN &&
                expr_is_var(ex->rgt->lft) && ex->rgt->lft->var == id )
            {
                vars *v = pgm_get_vars( expr_get_program(ex) );
                char buf[256];
                int len = sprintf(buf, "%.128s = %.12g", vars_get_long_name(v, id), val);
                ex->stmt = STMT_REM_HIDDEN;
                ex->rgt = expr_new_data(ex->mngr, (const uint8_t *)buf, len, 0);
                info_print(expr_get_file_name(ex), expr_get_file_line(ex),
                           "removing variable assignment for '%s'.\n",
                           vars_get_long_name(v, id));
                rep ++;
            }
        }
    return rep;
}

// Replace variable with a constant value
static int do_replace_var(expr *ex, int id, double val)
{
    int num = 0;

    if( ex )
    {
        if( expr_is_var(ex) && ex->var == id )
        {
            assert( !ex->lft && !ex->rgt && ex->type == et_var_number );
            ex->type = et_c_number;
            ex->num = val;
            return 1;
        }
        else
        {
            num += do_replace_var(ex->lft, id, val);
            num += do_replace_var(ex->rgt, id, val);
        }

    }
    return num;
}

static const char *var_name(const var_usage *vu)
{
    static char buf[256];
    strncpy(buf, vu->name, 250);
    if( vu->type == vtString )
        strcat(buf, "$");
    else if( vu->type == vtLabel )
    {
        memmove(buf + 1, buf, 255);
        buf[0] = '#';
    }
    else if( vu->type == vtArray )
        strcat(buf, "()");
    else if( vu->type != vtFloat )
        strcat(buf, "?");
    return buf;
}

static var_list *create_var_list(expr *prog)
{
    // Create the array with variable statistics
    var_list *vl = darray_new(var_usage, 16);
    vars *v = pgm_get_vars( expr_get_program(prog) );
    for(int i=0; i<vars_get_total(v); i++)
    {
        var_usage vu;
        vu.name = vars_get_long_name(v, i);
        vu.type = vars_get_type(v, i);
        vu.read = vu.written = vu.total = 0;
        vu.new_id = 0;
        vu.replace = 0;
        vu.rep_val = 0;
        vu.rep_line = -1;
        darray_add(vl,vu);
    }
    return vl;
}

int opt_remove_unused_vars(expr *prog)
{
    if( !prog )
        return 0;

    var_list *vl = create_var_list(prog);

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

int opt_replace_fixed_vars(expr *prog)
{
    if( !prog )
        return 0;

    var_list *vl = create_var_list(prog);

    int do_again = 1;
    while(do_again)
    {
        do_again = 0;
        // First pass - get totals
        do_get_var_usage(prog, vl);

        // Second pass - detailed count
        if( do_detail_var_usage(prog, vl) )
            err_print(expr_get_file_name(prog), 0, "detail var usage returned error.\n");


        var_usage *vu;
        darray_foreach(vu, vl)
        {
            if( vu->total != vu->read + vu->written )
            {
                err_print(expr_get_file_name(prog), 0,
                          "invalid var count for %s, %d + %d != %d.\n",
                          vu->name, vu->read, vu->written, vu->total);
            }
            else if( !vu->written && vu->read )
            {
                if( vu->type != vtFloat )
                {
                    info_print(expr_get_file_name(prog), 0,
                               "variable '%s' never written.\n", var_name(vu));
                }
                else
                {
                    warn_print(expr_get_file_name(prog), 0,
                            "variable '%s' never written, will replace with 0.\n",
                            var_name(vu));
                    vu->replace = 1;
                    vu->rep_val = 0.0;
                    do_again = 1;
                }
            }
            else if( vu->written == 1 && vu->read && vu->type == vtFloat && vu->rep_line >= 0 )
            {
                warn_print(expr_get_file_name(prog), vu->rep_line,
                           "variable '%s' written once in this line, will replace with %.12g, please check.\n",
                            var_name(vu), vu->rep_val);
                vu->replace = 1;
                do_again = 1;
            }
            else if( vu->written && !vu->read )
                info_print(expr_get_file_name(prog), 0, "variable '%s' never read.\n", var_name(vu));
        }

        // Perform the replacement
        if( do_again )
        {
            do_again = 0;
            for( int id=0; id<darray_len(vl); id++ )
            {
                vu = &darray_i(vl, id);
                if( vu->replace )
                {
                    if( do_replace_var_assign(prog, id, vu->rep_val) > 1 )
                        err_print(expr_get_file_name(prog), 0,
                                  "error replacing variable '%s'.\n",
                                  var_name(vu));

                    int num = do_replace_var(prog, id, vu->rep_val);
                    info_print(expr_get_file_name(prog), 0, "variable '%s' replaced at %d locations.\n",
                               var_name(vu), num);
                    do_again |= (num != 0);
                    vu->replace = 0;
                }
            }
            if( do_again )
            {
                // Reset statistics
                darray_foreach(vu, vl)
                    vu->read = vu->written = vu->total = 0;
            }
        }
    }
    darray_free(vl);

    // Finally, remove all unused variables
    return opt_remove_unused_vars(prog);
}

