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

#include "lowerexpr.h"
#include "expr.h"
#include "dbg.h"
#include "program.h"
#include "defs.h"
#include "vars.h"
#include "optconst.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

// Simplify printing warnings and errors
#define warn(...) \
    do { warn_print(expr_get_file_name(ex), expr_get_file_line(ex), __VA_ARGS__); } while(0)
#define error(...) \
    do { err_print(expr_get_file_name(ex), expr_get_file_line(ex), __VA_ARGS__); } while(0)

// Check if expr is given token
static int expr_is_tok(expr *ex, enum enum_tokens tk)
{
    return ex && ex->type == et_tok && ex->tok == tk;
}

// Check if expr is constant number
static int expr_is_cnum(expr *ex)
{
    return ex && (ex->type == et_c_number || ex->type == et_c_hexnumber);
}

// Check if expr is an string
static int expr_is_string(expr *ex)
{
    if( !ex )
        return 0;
    switch( ex->type )
    {
        case et_c_string:
        case et_var_string:
        case et_def_string:
            return 1;
        case et_tok:
            switch( ex->tok )
            {
                case TOK_STRP:
                case TOK_CHRP:
                case TOK_INKEYP:
                case TOK_HEXP:
                case TOK_TIMEP:
                    return 1;
                default:
                    return 0;
            }
            return 0;
        default:
            return 0;
    }
    return 0;
}

// Used to store information about the range of a numeric expression,
// this allows to optimize for integer results
typedef struct struct_exp_range {
    double low;
    double hi;
    double prec;
} exp_range;

static int to_i16(double d)
{
    return d < 0 ? 0 : d > 65535 ? 65535 : (int)(d+0.5);
}

static int max_bits(int x)
{
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    return x;
}

static exp_range range_i16(exp_range r)
{
    return (exp_range){to_i16(r.low), to_i16(r.hi), 1};
}

static exp_range range_and(exp_range a, exp_range b)
{
    a = range_i16(a);
    b = range_i16(b);
    return (exp_range){0, fmin(a.hi,b.hi), 1};
}

static exp_range range_or(exp_range a, exp_range b)
{
    a = range_i16(a);
    b = range_i16(b);
    int max = max_bits((int)a.hi | (int)b.hi);
    return (exp_range){fmin(a.low, b.low), max, 1};
}

static exp_range range_xor(exp_range a, exp_range b)
{
    a = range_i16(a);
    b = range_i16(b);
    int max = max_bits((int)a.hi | (int)b.hi);
    return (exp_range){0, max, 1};
}

static exp_range range_abs(exp_range r)
{
    if( r.low <= 0 && r.hi >= 0 )
        return (exp_range){0, fmax(-r.low,r.hi), r.prec};
    else if( r.low < 0 )
        return (exp_range){-r.hi, -r.low, r.prec};
    return r;
}

static exp_range range_neg(exp_range r)
{
    return (exp_range){-r.hi, -r.low, r.prec};
}

static exp_range range_int(exp_range r)
{
    return (exp_range){floor(r.low), floor(r.hi), 1};
}

static exp_range range_trunc(exp_range r)
{
    return (exp_range){trunc(r.low), trunc(r.hi), 1};
}

static exp_range range_rand(exp_range r)
{
    return (exp_range){0, floor(r.hi), 1};
}

static exp_range range_mult(exp_range a, exp_range b)
{
    double w = a.low * b.low, x = a.low * b.hi, y = a.hi * b.low, z = a.hi * b.hi;
    return (exp_range){fmin(fmin(w,x),fmin(y,z)),
                       fmax(fmax(w,x),fmax(y,z)),
                       a.prec * b.prec};
}

static exp_range range_add(exp_range a, exp_range b)
{
    return (exp_range){a.low+b.low,a.hi+b.hi,fmin(a.prec,b.prec)};
}

static exp_range range_sub(exp_range a, exp_range b)
{
    return (exp_range){a.low-b.hi,a.hi-b.low,fmin(a.prec,b.prec)};
}

static exp_range range_const(double x)
{
    const double limit = 1.0/4294967296.0;
    if( x == 0 )
        return (exp_range){x,x,1.0/limit};

    double prec = 1;
    while( floor(prec*x) != (prec*x) )
        prec *= 2;
    if( prec > 1 )
        return (exp_range){x,x,1.0/prec};
    while( prec>limit && floor(0.5*prec*x) == (0.5*prec*x) )
        prec *= 0.5;
    return (exp_range){x,x,1.0/prec};
}

static exp_range expr_get_range(expr *ex)
{
    if( !ex )
        return (exp_range){0,0,0};
    switch( ex->type )
    {
        case et_c_string:
        case et_var_string:
        case et_def_string:
        case et_def_number:
        case et_stmt:
        case et_lnum:
        case et_data:
        case et_void:
        case et_var_label:
            assert(!"expr type");
            return (exp_range){0,0,0};

        case et_c_number:
        case et_c_hexnumber:
            return range_const(ex->num);
        case et_var_number: // TODO: we need value propagation and integer vars
        case et_var_array:
            return (exp_range){-1e99,1e99,1e-99};
        case et_var_asmlabel: // TODO: some are 8bit
            return (exp_range){0,65535,1};
        case et_tok:
            switch( ex->tok )
            {
                case TOK_AND:
                case TOK_N_EQ:
                case TOK_N_GE:
                case TOK_N_GEQ:
                case TOK_N_LE:
                case TOK_N_LEQ:
                case TOK_N_NEQ:
                case TOK_NOT:
                case TOK_OR:
                case TOK_PTRIG:
                case TOK_S_EQ:
                case TOK_S_GE:
                case TOK_S_GEQ:
                case TOK_S_LE:
                case TOK_S_LEQ:
                case TOK_S_L_PRN:
                case TOK_S_NEQ:
                case TOK_STRIG:
                    return (exp_range){0,1,1};

                case TOK_PER_0:
                    return range_const(0);
                case TOK_PER_1:
                    return range_const(1);
                case TOK_PER_2:
                    return range_const(2);
                case TOK_PER_3:
                    return range_const(3);

                case TOK_ABS:
                    return range_abs( expr_get_range(ex->rgt) );

                case TOK_ADR:
                case TOK_DEC:
                case TOK_DPEEK:
                case TOK_ERL:
                case TOK_FRE:
                case TOK_INSTR:
                case TOK_LEN:
                case TOK_REG_AX:
                case TOK_UINSTR:
                case TOK_USR:
                    return (exp_range){0,65535,1};

                case TOK_ASC:
                case TOK_ERR:
                case TOK_PEEK:
                case TOK_PADDLE:
                case TOK_REG_AL:
                    return (exp_range){0,255,1};

                case TOK_STICK:
                    return (exp_range){0,15,1};

                case TOK_ANDPER:
                    return range_and(expr_get_range(ex->rgt), expr_get_range(ex->lft));
                case TOK_EXCLAM:
                    return range_or(expr_get_range(ex->rgt), expr_get_range(ex->lft));
                case TOK_EXOR:
                    return range_xor(expr_get_range(ex->rgt), expr_get_range(ex->lft));

                case TOK_ATN:
                case TOK_CLOG:
                case TOK_LOG:
                case TOK_VAL:
                case TOK_SLASH: // TODO: special cases?
                case TOK_DIV:   // TODO: special cases?
                case TOK_MOD:   // TODO: special cases?
                    return (exp_range){-1e99,1e99,1e-99};

                case TOK_EXP:
                case TOK_SQR:
                case TOK_CARET: // TODO: special cases?
                    return (exp_range){0,1e99,1e-99};

                case TOK_STAR:
                    return range_mult(expr_get_range(ex->rgt), expr_get_range(ex->lft));
                case TOK_MINUS:
                    return range_sub(expr_get_range(ex->rgt), expr_get_range(ex->lft));
                case TOK_PLUS:
                    return range_add(expr_get_range(ex->rgt), expr_get_range(ex->lft));

                case TOK_COS:
                case TOK_SIN:
                case TOK_RND:
                case TOK_RND_S:
                case TOK_FRAC: // TODO: positive if arg positive
                    return (exp_range){0,1,1e-99};
                case TOK_SGN:
                    return (exp_range){-1,1,1};

                case TOK_INT:
                    return range_int(expr_get_range(ex->rgt));
                case TOK_TRUNC:
                    return range_trunc(expr_get_range(ex->rgt));
                case TOK_RAND:
                    return range_rand(expr_get_range(ex->rgt));

                case TOK_TIME:
                    return (exp_range){0,16777216,1};

                case TOK_UMINUS:
                    return range_neg(expr_get_range(ex->rgt));

                case TOK_UPLUS:
                    return expr_get_range(ex->rgt);
                default:
                    assert(!"expr tok");
                    return (exp_range){0,0,0};
            }
    }
    assert(!"invalid range");
}

// Returns the "classification" of the expression values:
// -1 = error
// 0 = double
// 1 = integer > 16bit or negative
// 2 = integer 16bit
// 3 = integer 8bit
static int expr_inttype(expr *ex)
{
    exp_range r = expr_get_range(ex);
    if( r.prec <= 0 )
        return -1;
    if( r.prec < 1 )
        return 0;
    if( r.low < 0 || r.hi > 65535 )
        return 1;
    if( r.hi > 255 )
        return 2;
    return 3;
}

// Check if expr is an integer (0-65535)
static int expr_is_int(expr *ex)
{
    if( !ex )
        return 0;
    switch( ex->type )
    {
        case et_c_number:
        case et_c_hexnumber:
            if( ex->num >= 0 && ex->num < 65536 && ex->num == (int)ex->num )
                return 1;
            return 0;
        case et_tok:
            switch( ex->tok )
            {
                case TOK_N_LEQ:
                case TOK_N_NEQ:
                case TOK_N_GEQ:
                case TOK_N_LE:
                case TOK_N_GE:
                case TOK_N_EQ:
                case TOK_OR:
                case TOK_AND:
                case TOK_NOT:
                case TOK_S_LEQ:
                case TOK_S_NEQ:
                case TOK_S_GEQ:
                case TOK_S_LE:
                case TOK_S_GE:
                case TOK_S_EQ:
                case TOK_USR:
                case TOK_ASC:
                case TOK_LEN:
                case TOK_ADR:
                case TOK_PEEK:
                case TOK_FRE:
                case TOK_SGN:
                case TOK_PADDLE:
                case TOK_STICK:
                case TOK_PTRIG:
                case TOK_STRIG:
                case TOK_DPEEK:
                case TOK_ANDPER:
                case TOK_EXCLAM:
                case TOK_INSTR:
                case TOK_EXOR:
                case TOK_DEC:
                case TOK_PER_0:
                case TOK_PER_1:
                case TOK_PER_2:
                case TOK_PER_3:
                case TOK_UINSTR:
                case TOK_ERR:
                case TOK_ERL:
                case TOK_REG_AX:
                case TOK_REG_AL:
                    return 1;
                    // TODO: check range of parameter
                case TOK_INT:
                case TOK_DIV:
                case TOK_MOD:
                case TOK_RAND:
                case TOK_TRUNC:
                    // TODO: check if parameters are integer, result should be integer!
                case TOK_STAR:
                case TOK_PLUS:
                case TOK_MINUS:
                case TOK_SLASH:
                case TOK_L_PRN:
                case TOK_R_PRN:
                case TOK_UPLUS:
                case TOK_UMINUS:
                case TOK_VAL:
                case TOK_ABS:
                    // 24 bit integer, too big
                case TOK_TIME:
                default:
                    return 0;
            }
            return 0;
        default:
            return 0;
    }
    return 0;
}

// Stores a string of statements
typedef struct {
    expr *first;
    expr *last;
} code;

#define CODE0 ((code){0,0})

static int count_params(expr *ex)
{
    if( !ex )
        return 0;
    else if( expr_is_tok(ex, TOK_COMMA) )
        return 1 + count_params(ex->lft);
    else
        return 1;
}

// Returns the Nth parameter to the statement
// Note that parameters are numbered from the right, so the last one is 0.
static expr *get_param(expr *ex, int num)
{
    assert(ex && num>=0);
    if( !num )
    {
        if( expr_is_tok(ex, TOK_COMMA) || expr_is_tok(ex,TOK_SEMICOLON) )
            return ex->rgt;
        else
            return ex;
    }
    assert(expr_is_tok(ex, TOK_COMMA) || expr_is_tok(ex,TOK_SEMICOLON));
    return get_param(ex->lft, num-1);
}

// Returns an expression for the address of a variable
static expr *get_var_adr(expr *ex)
{
    assert(ex);
    assert(ex->type == et_var_string || ex->type == et_var_number || ex->type == et_var_array);
    return expr_new_asm_label(ex->mngr, ex->var);
}

// Returns an expression for an ASM label
static expr *get_asm_label(expr *ex, const char *name)
{
    vars *v = pgm_get_vars(expr_get_program(ex));
    int id = vars_search(v, name, vtAsmLabel);
    if( id < 0 )
    {
        id = vars_new_var(v, name, vtAsmLabel, 0, 0);
        assert(id>=0);
    }
    return expr_new_asm_label(ex->mngr, id);
}

// Generates code for a statement
static code add_stmt(expr *ex, enum enum_statements st, expr *toks, code cd)
{
    cd.last = expr_new_stmt(ex->mngr, cd.last, toks, st);
    if( !cd.first )
        cd.first = cd.last;
    return cd;
}

// Generates a statement to load REG-AL with an 8bit value from an expression
static code expr_load_AL(expr *ex, code cd)
{
    expr *toks = expr_new_bin(ex->mngr, expr_new_tok(ex->mngr, TOK_REG_AL),
                              ex, TOK_I_ASGN);
    return add_stmt(ex, STMT_LET, toks, cd);
}

// Generates a statement to load REG-AL with a boolean value from an expression
static code expr_load_bool_AL(expr *ex, code cd)
{
    expr *toks = expr_new_bin(ex->mngr, expr_new_tok(ex->mngr, TOK_REG_AL),
                              ex, TOK_B_ASGN);
    return add_stmt(ex, STMT_LET, toks, cd);
}

// Generates a statement to load REG-AX with a 16bit value from an expression
static code expr_load_AX(expr *ex, code cd)
{
    expr *toks = expr_new_bin(ex->mngr, expr_new_tok(ex->mngr, TOK_REG_AX),
                              ex, TOK_I_ASGN);
    return add_stmt(ex, STMT_LET, toks, cd);
}

// Generates a statement to load FR0 from an FP expression
static code expr_load_FR0(expr *ex, code cd)
{
    expr *toks = expr_new_bin(ex->mngr, get_asm_label(ex, "FR0"), ex, TOK_F_ASGN);
    return add_stmt(ex, STMT_LET, toks, cd);
}

// Generates a statement to load FR1 from an FP expression
static code expr_load_FR1(expr *ex, code cd)
{
    expr *toks = expr_new_bin(ex->mngr, get_asm_label(ex, "FR1"), ex, TOK_F_ASGN);
    return add_stmt(ex, STMT_LET, toks, cd);
}

// Generates a statement to store AL to an asm label
static code expr_store_AL(expr *ex, const char *name, code cd)
{
    expr *toks = expr_new_bin(ex->mngr, get_asm_label(ex, name),
                              expr_new_tok(ex->mngr, TOK_REG_AL), TOK_I_ASGN);
    return add_stmt(ex, STMT_LET, toks, cd);
}

// Generates a statement to store AL to an address
static code expr_store_AL_addr(expr *ex, int addr, code cd)
{
    expr *toks = expr_new_bin(ex->mngr, expr_new_number(ex->mngr, addr),
                              expr_new_tok(ex->mngr, TOK_REG_AL), TOK_I_ASGN);
    return add_stmt(ex, STMT_LET, toks, cd);
}

// Generates a statement to store AL to an indirect address
static code expr_store_AL_ind(expr *ex, const char *name, code cd)
{
    expr *toks = expr_new_bin(ex->mngr, get_asm_label(ex, name),
                              expr_new_tok(ex->mngr, TOK_REG_AL), TOK_I_XSTO);
    return add_stmt(ex, STMT_LET, toks, cd);
}

// Generates a statement to store AX to an asm label
static code expr_store_AX(expr *ex, const char *name, code cd)
{
    expr *toks = expr_new_bin(ex->mngr, get_asm_label(ex, name),
                              expr_new_tok(ex->mngr, TOK_REG_AX), TOK_I_ASGN);
    return add_stmt(ex, STMT_LET, toks, cd);
}

// Generates a statement to store AX to an address
static code expr_store_AX_addr(expr *ex, int addr, code cd)
{
    expr *toks = expr_new_bin(ex->mngr, expr_new_number(ex->mngr, addr),
                              expr_new_tok(ex->mngr, TOK_REG_AX), TOK_I_ASGN);
    return add_stmt(ex, STMT_LET, toks, cd);
}

// Generates a statement to store AX to an indirect address
static code expr_store_AX_ind(expr *ex, const char *name, code cd)
{
    expr *toks = expr_new_bin(ex->mngr, get_asm_label(ex, name),
                              expr_new_tok(ex->mngr, TOK_REG_AX), TOK_I_XSTO);
    return add_stmt(ex, STMT_LET, toks, cd);
}

// Generates a statement to call a routine with a 16bit value in reg-AX
static code expr_call_16(expr *ex, const char *name, code cd)
{
    cd = expr_load_AX(ex, cd);
    return add_stmt(ex, STMT_EXEC_ASM, get_asm_label(ex, name), cd);
}

// Generates a statement to call a routine with an 8bit value in reg-A
static code expr_call_8(expr *ex, const char *name, code cd)
{
    cd = expr_load_AL(ex, cd);
    return add_stmt(ex, STMT_EXEC_ASM, get_asm_label(ex, name), cd);
}

// Generates a statement to call a routine with no arguments
static code expr_call(expr *ex, const char *name, code cd)
{
    return add_stmt(ex, STMT_EXEC_ASM, get_asm_label(ex, name), cd);
}

// Stores the Nth parameter, removing th "#" before the I/O channel,
// to the location used for specifying I/O channel:
static code get_param_io(expr *ex, int num, code cd)
{
    expr *p = get_param(ex, num);
    assert(expr_is_tok(p, TOK_SHARP));
    cd = expr_load_AL(p->rgt, cd);
    return expr_store_AL(ex, "bas_io_channel", cd);
}

static code get_const_io(expr *ex, int num, code cd)
{
    cd = expr_load_AL(expr_new_number(ex->mngr, num), cd);
    return expr_store_AL(ex, "bas_io_channel", cd);
}

// Replace an expression with the given one
static int expr_replace(expr *old, code ncode)
{
    if( !ncode.first || !ncode.last )
        return 1;

    expr *e = ncode.first;
    // Swap old with e
    expr tmp;
    tmp = *e;
    *e = *old;
    *old = tmp;

    // Link
    while(old->lft)
        old = old->lft;

    old->lft = e->lft;
    e->lft = 0;
    return 0;
}

// Statement with one 16bit (string/number) parameter
static int lower_call_16_expr(expr *ex, const char *name)
{
    return expr_replace(ex, expr_call_16(get_param(ex->rgt,0), name, CODE0));
}

// Statement with one 8bit number parameter
static int lower_call_8_expr(expr *ex, const char *name)
{
    return expr_replace(ex, expr_call_8(get_param(ex->rgt,0), name, CODE0));
}

// Statement without parameters
static int lower_call_nul_expr(expr *ex, const char *name)
{
    return expr_replace(ex, expr_call(ex, name, CODE0));
}

// IF expression
static int lower_if_expr(expr *ex)
{
    assert(ex && ex->rgt && ex->rgt->rgt);
    assert(ex->rgt->rgt->type == et_var_label);
    code cd = CODE0;
    cd = expr_load_bool_AL(ex->rgt->lft, cd);
    cd = add_stmt(ex, STMT_JUMP_COND, ex->rgt->rgt, cd);
    return expr_replace(ex, cd);
}

// Stores two parameters to ROWCRS/COLCRS, used in PLOT/DRAWTO, etc.
static code expr_x_y_param(expr *ex, int num)
{
    expr *px = get_param(ex, num + 1);
    expr *py = get_param(ex, num);
    // Stores X,Y in COLCRS/ROWCRS in PAR1 / PAR2, as 16bit
    code cd = CODE0;
    cd = expr_load_AL(py, cd);
    cd = expr_store_AL(ex, "ROWCRS", cd);
    cd = expr_load_AX(px, cd);
    cd = expr_store_AX(ex, "COLCRS", cd);
    return cd;
}

// PLOT/DRAWTO/FILLTO
static int lower_call_xy_expr(expr *ex, const char *name)
{
    code cd = expr_call(ex, name, expr_x_y_param(ex->rgt, 0));
    return expr_replace(ex, cd);
}

// COLOR/FCOLOR - store 8bit expr to "name"
static int lower_sto_8_expr(expr *ex, const char *name)
{
    code cd = expr_load_AL(get_param(ex->rgt,0), CODE0);
    cd = expr_store_AL(ex, name, cd);
    return expr_replace(ex, cd);
}

// DEG/RAD: store 8bit constant to "name"
static int lower_sto_8_const(expr *ex, const char *name, int num)
{
    code cd = expr_load_AL(expr_new_number(ex->mngr, num), CODE0);
    cd = expr_store_AL(ex, name, cd);
    return expr_replace(ex, cd);
}

// CLOSE
static int lower_call_io_expr(expr *ex, const char *name)
{
    return expr_replace(ex, expr_call(ex, name, get_param_io(ex->rgt, 0, CODE0)));
}

// BGET/BPUT/POINT
static int lower_io_num_num_expr(expr *ex, const char *name)
{
    expr *p2 = get_param(ex->rgt, 1);
    expr *p3 = get_param(ex->rgt, 0);
    // Set IO channel
    code cd = get_param_io(ex->rgt, 2, CODE0);
    // Stores p3 in PAR1, as 16bit
    cd = expr_load_AX(p3, cd);
    cd = expr_store_AX(ex, "bas_param_1", cd);
    // Calls with p2
    cd = expr_call_16(p2, name, cd);
    return expr_replace(ex, cd);
}

// OPEN: IO, AUX1, AUX2, SPEC
static int lower_open_expr(expr *ex, const char *name)
{
    expr *p2 = get_param(ex->rgt, 2);
    expr *p3 = get_param(ex->rgt, 1);
    expr *p4 = get_param(ex->rgt, 0);
    // Set IO channel
    code cd = get_param_io(ex->rgt, 3, CODE0);
    // Stores p2/p3 in PAR1, PAR2, as 8bit
    cd = expr_load_AL(p2, cd);
    cd = expr_store_AL(ex, "bas_param_1", cd);
    cd = expr_load_AL(p3, cd);
    cd = expr_store_AL(ex, "bas_param_2", cd);
    cd = expr_call_16(p4, name, cd);
    return expr_replace(ex, cd);
}

// POKE low level
static int lower_poke_stmt(expr *ex, expr *addr, expr *val)
{
    if( expr_is_cnum(addr) )
        return expr_replace(ex, expr_store_AL_addr(ex, addr->num, expr_load_AL(val, CODE0)) );

    code cd = CODE0;
    cd = expr_load_AX(addr, cd);
    cd = expr_store_AX(ex, "bas_param_1", cd);
    cd = expr_load_AL(val, cd);
    cd = expr_store_AL_ind(ex, "bas_param_1", cd);
    return expr_replace( ex, cd);
}

// DPOKE low level
static int lower_dpoke_stmt(expr *ex, expr *addr, expr *val)
{
    if( expr_is_cnum(addr) )
        return expr_replace(ex, expr_store_AX_addr(ex, addr->num, expr_load_AX(val, CODE0)) );

    code cd = CODE0;
    cd = expr_load_AX(addr, cd);
    cd = expr_store_AX(ex, "bas_param_1", cd);
    cd = expr_load_AX(val, cd);
    cd = expr_store_AX_ind(ex, "bas_param_1", cd);
    return expr_replace( ex, cd);
}

// CIRCLE: x,y,rx,ry
static int lower_circle_expr(expr *ex)
{
    if( count_params(ex->rgt) == 4 )
    {
        // x, y, rx, ry
        code cd = expr_x_y_param(ex->rgt, 2);
        cd = expr_load_AL(get_param(ex->rgt, 0), cd);
        cd = expr_store_AL(ex, "bas_param_1", cd);
        cd = expr_call_8(get_param(ex->rgt, 1), "bas_ellipse", cd);
        return expr_replace(ex, cd);
    }
    else
    {
        // x, y, r
        code cd = expr_x_y_param(ex->rgt, 1);
        cd = expr_call_8(get_param(ex->rgt, 0), "bas_circle", cd);
        return expr_replace(ex, cd);
    }
}

// Used in "flag" instructions: *B, *F, *L
static int lower_sto_flag_expr(expr *ex, const char *name)
{
    if( ex && expr_is_tok(ex->rgt, TOK_MINUS) )
        return lower_sto_8_const(ex, name, 0);
    else
        return lower_sto_8_const(ex, name, 1);
}

static int lower_move_expr(expr *ex, const char *name)
{
    // Calls MOVE with source in param_2, target in param_1 and
    // length in AX:
    expr *dst = get_param(ex->rgt, 2);
    expr *src = get_param(ex->rgt, 1);
    expr *len = get_param(ex->rgt, 0);
    // Stores p2/p3 in PAR1 / PAR2, as 16bit
    code cd = CODE0;
    cd = expr_load_AX(dst, cd);
    cd = expr_store_AX(ex, "bas_param_1", cd);
    cd = expr_load_AX(src, cd);
    cd = expr_store_AX(ex, "bas_param_2", cd);
    cd = expr_call_16(len, name, cd);
    return expr_replace(ex, cd);
}

static int lower_sound_expr(expr *ex, const char *name, int dsound)
{
    if( !ex->rgt )
        return lower_call_nul_expr(ex, "bas_sound_off");

    expr *chn = get_param(ex->rgt, 3);
    expr *frq = get_param(ex->rgt, 2);
    expr *dst = get_param(ex->rgt, 1);
    expr *vol = get_param(ex->rgt, 0);

    // Build an expression to calculate the AUDC value:
    //  dst = dst & 255  (this forces 8bit integer arithmetic)
    dst = expr_new_bin(ex->mngr, dst, expr_new_number(ex->mngr, 255), TOK_ANDPER);
    //  dst = dst * 16
    dst = expr_new_bin(ex->mngr, dst, expr_new_number(ex->mngr, 16), TOK_STAR);
    //  dst = dst | vol
    dst = expr_new_bin(ex->mngr, dst, vol, TOK_EXCLAM);

    // Get channel address
    //  chn = (chn & 3) * 2  (sound)  ;   chn = (chn & 1) * 4  (dsound)
    chn = expr_new_bin(ex->mngr, chn, expr_new_number(ex->mngr, dsound?1:3), TOK_ANDPER);
    chn = expr_new_bin(ex->mngr, chn, expr_new_number(ex->mngr, dsound?4:2), TOK_STAR);

    code cd = CODE0;
    // Stores channel in PARAM 2
    cd = expr_load_AL(chn, cd);
    cd = expr_store_AL(ex, "bas_param_2", cd);
    // Stores frequency in PAR1, as 8 or 16 bit
    if( dsound )
    {
        cd = expr_load_AX(frq, cd);
        cd = expr_store_AX(ex, "bas_param_1", cd);
    }
    else
    {
        cd = expr_load_AL(frq, cd);
        cd = expr_store_AL(ex, "bas_param_1", cd);
    }
    // Call with 8 bit parameter (AUDC)
    cd = expr_call_8(dst, name, cd);
    return expr_replace(ex, cd);
}

static int lower_dim_expr(expr *ex)
{
    // Assume DIM with only one variable
    assert(ex->rgt && ex->rgt->type == et_tok);
    if( ex->rgt->tok == TOK_DS_L_PRN && ex->rgt->lft->type == et_var_string )
    {
        code cd = CODE0;
        cd = expr_load_AX(ex->rgt->rgt, cd);
        cd = expr_store_AX(ex, "bas_param_1", cd);
        cd = expr_call_16(ex->rgt->lft , "dim_string", cd);
        return expr_replace(ex, cd);
    }
    if( ex->rgt->tok == TOK_D_L_PRN && ex->rgt->lft->type == et_var_array )
    {
        if( expr_is_tok(ex->rgt->rgt, TOK_A_COMMA) )
        {
            code cd = CODE0;
            cd = expr_load_AX(ex->rgt->rgt->rgt, cd);
            cd = expr_store_AX(ex, "bas_param_1", cd);
            cd = expr_load_AX(ex->rgt->rgt->lft, cd);
            cd = expr_store_AX(ex, "bas_param_2", cd);
            cd = expr_call_16(ex->rgt->lft , "dim_array_2d", cd);
            return expr_replace(ex, cd);
        }
        else
        {
            code cd = CODE0;
            cd = expr_load_AX(ex->rgt->rgt, cd);
            cd = expr_store_AX(ex, "bas_param_1", cd);
            cd = expr_call_16(ex->rgt->lft , "dim_array_1d", cd);
            return expr_replace(ex, cd);
        }
    }
    else
        assert("invalid DIM");
    return 1;
}

static int lower_put_expr(expr *ex)
{
    expr *list = ex->rgt;
    code cd;
    if( list && expr_is_tok(list->lft, TOK_SHARP) )
    {
        cd = get_param_io(list->lft, 0, CODE0);
        list = list->rgt; // Go to list of parameters
    }
    else
        cd = get_const_io(ex, 0, CODE0);

    int n = count_params(list);
    while(--n>=0)
        cd = expr_call_8(get_param(list, n), "bas_putchar", cd);
    return expr_replace(ex, cd);
}

static int lower_pput_expr(expr *ex)
{
    expr *list = ex->rgt;
    code cd;
    if( list && expr_is_tok(list->lft, TOK_SHARP) )
    {
        cd = get_param_io(list->lft, 0, CODE0);
        list = list->rgt; // Go to list of parameters
    }
    else
        cd = get_const_io(ex, 0, CODE0);

    int n = count_params(list);
    while(--n>=0)
    {
        cd = expr_load_FR0(get_param(list, n), cd);
        cd = expr_call(ex, "bas_put_fp", cd);
    }
    return expr_replace(ex, cd);
}

static int lower_get_expr(expr *ex)
{
    expr *list = ex->rgt;
    code cd;
    const char *name;
    if( list && expr_is_tok(list->lft, TOK_SHARP) )
    {
        cd = get_param_io(list->lft, 0, CODE0);
        list = list->rgt; // Go to list of parameters
        name = "bas_getchar";
    }
    else
    {
        cd = CODE0;
        name = "bas_getkey";
    }
    int n = count_params(list);
    while(--n>=0)
    {
        cd = expr_call(ex, name, cd);
        expr *toks = expr_new_bin(ex->mngr, get_param(list,n),
                                  expr_new_tok(ex->mngr, TOK_REG_AX), TOK_F_ASGN);
        cd = add_stmt(ex, STMT_LET, toks, cd);
    }
    return expr_replace(ex, cd);
}

static int lower_pget_expr(expr *ex)
{
    expr *toks = ex->rgt;
    code cd;
    if( !toks || !expr_is_tok(toks->lft, TOK_SHARP) )
    {
        error("%%GET without #IOCB is invalid\n");
        ex->stmt = STMT_REM_;
        return 1;
    }

    expr *list = toks->rgt;
    int n = count_params(list);
    cd = get_param_io(toks->lft, 0, CODE0);
    while(--n>=0)
    {
        cd = expr_call(ex, "bas_get_fp", cd);
        expr *toks = expr_new_bin(ex->mngr, get_param(list,n),
                                  get_asm_label(ex, "FR0"), TOK_F_ASGN);
        cd = add_stmt(ex, STMT_LET, toks, cd);
    }
    return expr_replace(ex, cd);
}

static int lower_status_expr(expr *ex)
{
    code cd = get_param_io(ex->rgt, 1, CODE0);
    cd = expr_call(ex, "bas_status", cd);
    expr *toks = expr_new_bin(ex->mngr, get_param(ex->rgt,0),
                              expr_new_tok(ex->mngr, TOK_REG_AX), TOK_F_ASGN);
    return expr_replace(ex, add_stmt(ex, STMT_LET, toks, cd));
}

static code get_code_print(expr *ex, code cd, int *eol)
{
    if( !ex )
        return cd;

    else if( expr_is_tok(ex, TOK_COMMA) )
    {
        cd = get_code_print(ex->lft, cd, eol);
        cd = expr_call(ex, "bas_print_comma", cd);
        *eol = 0;
        cd = get_code_print(ex->rgt, cd, eol);
    }
    else if( expr_is_tok(ex, TOK_SEMICOLON) )
    {
        cd = get_code_print(ex->lft, cd, eol);
        *eol = 0;
        cd = get_code_print(ex->rgt, cd, eol);
    }
    else if( expr_is_tok(ex, TOK_SHARP) )
    {
        return cd;
    }
    else if( expr_is_string(ex) )
    {
        cd = expr_load_AX(ex, cd);
        cd = expr_call(ex, "bas_print_str", cd);
        *eol = 1;
    }
    else
    {
        // Determine if we have integer argument:
        if( expr_is_int(ex) )
        {
            cd = expr_load_AX(ex, cd);
            cd = expr_call(ex, "bas_print_AX", cd);
        }
        else
        {
            cd = expr_load_FR0(ex, cd);
            cd = expr_call(ex, "bas_print_FR0", cd);
        }
        *eol = 1;
    }
    return cd;
}

static int lower_print_expr(expr *ex)
{
    code cd = CODE0;

    expr *tk = ex->rgt;
    if( expr_is_tok(tk, TOK_SHARP) )
        cd = get_param_io(tk, 0, cd);
    else if( tk && expr_is_tok(tk->lft, TOK_SHARP) )
        cd = get_param_io(tk->lft, 0, cd);
    else
        cd = get_const_io(ex, 0, cd);

    int eol = 1;
    cd = get_code_print(tk, cd, &eol);
    if( eol )
        cd = expr_call_8(expr_new_number(ex->mngr, 155), "bas_putchar", cd);
    return expr_replace(ex, cd);
}

static int lower_lprint_expr(expr *ex)
{
    code cd = CODE0;
    cd = expr_call(ex, "bas_lprint_open", cd);
    int eol = 1;
    cd = get_code_print(ex->rgt, cd, &eol);
    if( eol )
        cd = expr_call_8(expr_new_number(ex->mngr, 155), "bas_putchar", cd);
    cd = expr_call(ex, "bas_lprint_close", cd);
    return expr_replace(ex, cd);
}

static int lower_let_farray_expr(expr *ex)
{
    // Split  var = expr    ->  param1 = ADR(var) : param1 (*)= expr
    code cd = CODE0;

    cd = add_stmt(ex, STMT_LET,
            expr_new_bin(ex->mngr, get_asm_label(ex, "bas_param_1"),
                         expr_new_uni(ex->mngr, ex->rgt->lft, TOK_ADR), TOK_I_ASGN),
                  cd);
    cd = add_stmt(ex, STMT_LET,
         expr_new_bin(ex->mngr, get_asm_label(ex, "bas_param_1"), ex->rgt->rgt, TOK_F_XSTO),
         cd );
    return expr_replace(ex, cd);
}

static int lower_let_str_expr(expr *ex)
{
    code cd = CODE0;
    cd = expr_load_AX(ex->rgt->lft, cd);
    cd = expr_store_AX(ex, "bas_param_1", cd);
    cd = expr_call_16(ex->rgt->rgt, "bas_str_asgn", cd);
    return expr_replace(ex, cd);
}

static int do_lower_stmt(expr *ex)
{
    assert(ex && ex->type == et_stmt);

    switch(ex->stmt)
    {
        case STMT_BAS_ERROR:
        case STMT_CLOAD:
        case STMT_CONT:
        case STMT_CSAVE:
        case STMT_DEL:
        case STMT_DUMP:
        case STMT_ENTER:
        case STMT_LIST:
        case STMT_LOAD:
        case STMT_RENUM:
        case STMT_RUN:
        case STMT_SAVE:
        case STMT_TRACE:
        case STMT_F_L:
            error("can't compile statement '%s'\n", statements[ex->stmt].stm_long);
            ex->stmt = STMT_REM_;
            return 1;

            // Statements that where already replaced
        case STMT_CLS:
        case STMT_SETCOLOR:
        case STMT_LET_INV:
        case STMT_PROC:
        case STMT_LOOP:
        case STMT_NEXT:
        case STMT_WEND:
        case STMT_UNTIL:
        case STMT_ELSE:
        case STMT_ENDIF:
        case STMT_ENDIF_INVISIBLE:
        case STMT_EXEC_PAR:
        case STMT_PROC_VAR:
        case STMT_EXIT:
        case STMT_COM:
        case STMT_DO:
        case STMT_REPEAT:
        case STMT_WHILE:
        case STMT_FOR:
        case STMT_IF_MULTILINE:
        case STMT_IF_THEN:
        case STMT_GOSUB:
        case STMT_GOTO:
        case STMT_GO_TO:
        case STMT_ENDPROC:
        case STMT_IF:
            error("invalid statement %d '%s'\n", ex->stmt, statements[ex->stmt].stm_long);
            ex->stmt = STMT_REM_;
            return 1;

            // Already converted
        case STMT_EXEC_ASM:
        case STMT_JUMP_COND:
        case STMT_GO_S:
        case STMT_LBL_S:
        case STMT_RETURN:
            return 0;

        case STMT_BGET:
            return lower_io_num_num_expr(ex, "bas_bget");
        case STMT_BLOAD:
            return lower_call_16_expr(ex, "bas_bload");
        case STMT_BPUT:
            return lower_io_num_num_expr(ex, "bas_bput");
        case STMT_BRUN:
            return lower_call_16_expr(ex, "bas_brun");
        case STMT_BYE:
            return lower_call_nul_expr(ex, "BLKBDV");
        case STMT_CIRCLE:
            return lower_circle_expr(ex);
        case STMT_CLOSE:
            if( ex->rgt )
                return lower_call_io_expr(ex, "bas_close");
            else
                return lower_call_nul_expr(ex, "bas_close_all");
        case STMT_CLR:
            return lower_call_nul_expr(ex, "bas_clr");
        case STMT_COLOR:
            return lower_sto_8_expr(ex, "COLOR");
        case STMT_DIM:
            return lower_dim_expr(ex);
        case STMT_DATA:
            // TODO
            return 1;
        case STMT_DEG:
            return lower_sto_8_const(ex, "RADFLG", 6);
        case STMT_DELETE:
            return lower_call_16_expr(ex, "bas_delete");
        case STMT_DIR:
            if( ex->rgt )
                return lower_call_16_expr(ex, "bas_dir");
            else
                return lower_call_nul_expr(ex, "bas_dir_all");
        case STMT_DOS:
            return lower_call_nul_expr(ex, "bas_dos");
        case STMT_DPOKE:
            return lower_dpoke_stmt(ex, ex->rgt->lft, ex->rgt->rgt);
        case STMT_DRAWTO:
            return lower_call_xy_expr(ex, "bas_drawto");
        case STMT_DSOUND:
            return lower_sound_expr(ex, "bas_dsound", 1);
        case STMT_END:
            return lower_call_nul_expr(ex, "bas_end");
        case STMT_EXEC:
            ex->stmt = STMT_EXEC_ASM;
            return 0;
        case STMT_FCOLOR:
            return lower_sto_8_expr(ex, "FILDAT");
        case STMT_FILLTO:
            return lower_call_xy_expr(ex, "bas_fillto");
        case STMT_F_B:
            return lower_sto_flag_expr(ex, "bas_brk_flag");
        case STMT_F_F:
            return lower_sto_flag_expr(ex, "bas_for_flag");
        case STMT_GET:
            return lower_get_expr(ex);
        case STMT_GRAPHICS:
            return lower_call_8_expr(ex, "bas_graphics");
        case STMT_IF_NUMBER:
            return lower_if_expr(ex);
        case STMT_INPUT:
            return 1;
        case STMT_LET:
            if( expr_is_tok(ex->rgt, TOK_F_ASGN) )
            {
                // Convert array assignments to indirect assignment:
                if( expr_is_tok(ex->rgt->lft, TOK_A_L_PRN) )
                    return lower_let_farray_expr(ex);
            }
            else if( expr_is_tok(ex->rgt, TOK_S_ASGN) )
            {
                // Call function for string assignment
                return lower_let_str_expr(ex);
            }
            return 0;
        case STMT_LOCATE:
            return 1;
        case STMT_LOCK:
            return lower_call_16_expr(ex, "bas_lock");
        case STMT_LPRINT:
            return lower_lprint_expr(ex);
        case STMT_MOVE:
            return lower_move_expr(ex, "bas_move");
        case STMT_NEW:
            return lower_call_nul_expr(ex, "bas_end");
        case STMT_NOTE:
            return 1;
        case STMT_N_MOVE:
            return lower_move_expr(ex, "bas_neg_move");
        case STMT_ON:
            return 1;
        case STMT_OPEN:
            return lower_open_expr(ex, "bas_open");
        case STMT_PAINT:
            return lower_call_xy_expr(ex, "bas_paint");
        case STMT_PAUSE:
            return lower_call_16_expr(ex, "bas_pause");
        case STMT_PLOT:
            return lower_call_xy_expr(ex, "bas_plot");
        case STMT_POINT:
            return lower_io_num_num_expr(ex, "bas_point");
        case STMT_POKE:
            return lower_poke_stmt(ex, ex->rgt->lft, ex->rgt->rgt);
        case STMT_POP:
            return 1;
        case STMT_POSITION:
            return expr_replace(ex, expr_x_y_param(ex->rgt, 0));
        case STMT_PRINT:
        case STMT_PRINT_:
            return lower_print_expr(ex);
        case STMT_PUT:
            return lower_put_expr(ex);
        case STMT_P_GET:
            return lower_pget_expr(ex);
        case STMT_P_PUT:
            return lower_pput_expr(ex);
        case STMT_RAD:
            return lower_sto_8_const(ex, "RADFLG", 0);
        case STMT_READ:
            return 1;
        case STMT_REM:
        case STMT_REM_:
            // Remove statement data
            ex->stmt = STMT_REM_;
            ex->rgt = 0;
            return 0;
        case STMT_RENAME:
            return lower_call_16_expr(ex, "bas_rename");
        case STMT_RESTORE:
            if( !ex->rgt )
                return lower_call_nul_expr(ex, "bas_restore_start");
            else if( expr_is_tok(ex->rgt, TOK_SHARP) )
                return expr_replace(ex, expr_call_16(ex->rgt->rgt, "bas_restore", CODE0));
            else
            {
                error("invalid RESTORE\n");
                return 1;
            }
        case STMT_SOUND:
            return lower_sound_expr(ex, "bas_sound", 0);
        case STMT_STATUS:
            return lower_status_expr(ex);
        case STMT_STOP:
            return lower_call_nul_expr(ex, "bas_end");
        case STMT_TEXT:
            return expr_replace(ex,
                    expr_call_16(get_param(ex->rgt,0), "bas_text", expr_x_y_param(ex->rgt,1)));
        case STMT_TIME_S:
            return lower_call_16_expr(ex, "bas_set_time_str");
        case STMT_TRAP:
            if( !ex->rgt )
                return lower_call_nul_expr(ex, "bas_trap_unset");
            else if( expr_is_tok(ex->rgt, TOK_SHARP) )
                return expr_replace(ex, expr_call_16(ex->rgt->rgt, "bas_trap", CODE0));
            else
            {
                error("invalid TRAP\n");
                return 1;
            }
        case STMT_UNLOCK:
            return lower_call_16_expr(ex, "bas_unlock");
        case STMT_XIO:
            return 1;
    }
    return 1;
}


static int expr_is_immed_int(expr *ex)
{
    if( !ex )
        return 1;
    switch( ex->type )
    {
        case et_c_string:
        case et_var_string:
        case et_var_label:
        case et_c_number:
        case et_c_hexnumber:
        case et_var_number:
        case et_var_asmlabel:
            return 1;
        case et_def_string:
        case et_def_number:
        case et_stmt:
        case et_lnum:
        case et_data:
        case et_void:
        case et_var_array:
            return 0;
        case et_tok:
            switch( ex->tok )
            {
                case TOK_ERL:
                case TOK_ERR:
                case TOK_PER_0:
                case TOK_PER_1:
                case TOK_PER_2:
                case TOK_PER_3:
                    return 1;
                case TOK_ADR: // TODO: how to deal with those (transform to "PEEK/DPEEK")
                case TOK_LEN:

                case TOK_PEEK: // When param is constant, those are constants themselves
                case TOK_PADDLE:
                case TOK_PTRIG:
                case TOK_STICK:
                case TOK_STRIG:

                case TOK_TIME:
                case TOK_REG_AL:
                case TOK_REG_AX:
                default:
                    return 0;
            }
    }
    return 0;
}

static code lower_i8_expr_rec(expr *ex, expr *dst, code cd)
{
    if( expr_is_cnum(ex) )
    {
        if(ex->num < 0 || ex->num > 255.5)
            error("8 bit number out of range\n");
        ex->num = (int)(0.5 + ex->num);
        return add_stmt(ex, STMT_LET, expr_new_bin(ex->mngr, dst, ex, TOK_I_ASGN), cd);
    }
    else if( ex->type == et_var_asmlabel )
        return add_stmt(ex, STMT_LET, expr_new_bin(ex->mngr, dst, ex, TOK_I_ASGN), cd);
    else if( ex->type == et_var_number || ex->type == et_var_array )
    {
        // Load FP value and convert to integer
        expr *fr0 = get_asm_label(ex, "FR0");
        cd = lower_fp_expr_rec(ex, fr0, cd);
        cd = expr_call(ex, "bas_fp_to_int8", cd);
        // Store to dest
        if( !expr_is_tok(dst, TOK_REG_AL) )
            cd = add_stmt(ex, STMT_LET,
                          expr_new_bin(ex->mngr, dst,
                                       expr_new_tok(ex->mngr, TOK_REG_AL), TOK_I_ASGN),
                          cd);
        return cd;
    }
    else if( ex->type == et_tok )
    {
        // Get expressions "integer range type"
        int itp_l = expr_inttype(ex->lft);
        int itp_r = expr_inttype(ex->rgt);
        int itype = (itp_l < itp_r) ? itp_l : itp_r;

        if( itype == 0 || itype == 1 )
        {
            expr *fr0 = get_asm_label(ex, "FR0");
            cd = lower_fp_expr_rec(ex, fr0, cd);
            cd = expr_call(ex, "bas_fp_to_int8", cd);
            // Store to dest
            if( !expr_is_tok(dst, TOK_REG_AL) )
                cd = add_stmt(ex, STMT_LET,
                        expr_new_bin(ex->mngr, dst,
                            expr_new_tok(ex->mngr, TOK_REG_AL), TOK_I_ASGN),
                        cd);
            return cd;
        }
        else if( itype == 2 )
        {
            expr *rax = expr_new_tok(ex->mngr, TOK_REG_AX);
            cd = lower_i16_expr_rec(ex, rax, cd);
            // Store to dest
            if( !expr_is_tok(dst, TOK_REG_AL) )
                cd = add_stmt(ex, STMT_LET,
                        expr_new_bin(ex->mngr, dst,
                            expr_new_tok(ex->mngr, TOK_REG_AL), TOK_I_ASGN),
                        cd);
            return cd;
        }
        assert( itype != 3 );

        switch(ex->tok)
        {
            case TOK_QUOTE:
            case TOK_DUMMY:
            case TOK_COMMA:
            case TOK_DOLAR:
            case TOK_COLON:
            case TOK_SEMICOLON:
            case TOK_EOL:
            case TOK_ON_GOTO:
            case TOK_ON_GOSUB:
            case TOK_FOR_TO:
            case TOK_STEP:
            case TOK_THEN:
            case TOK_SHARP:
            case TOK_L_PRN:
            case TOK_R_PRN:
            case TOK_F_ASGN:
            case TOK_S_ASGN:
            case TOK_S_L_PRN:
            case TOK_A_L_PRN:
            case TOK_D_L_PRN:
            case TOK_FN_PRN:
            case TOK_DS_L_PRN:
            case TOK_A_COMMA:
            case TOK_I_ASGN:
            case TOK_B_ASGN:
            case TOK_F_XSTO:
            case TOK_I_XSTO:
            case TOK_PER_0:
            case TOK_PER_1:
            case TOK_PER_2:
            case TOK_PER_3:
            case TOK_ON_GOSHARP:
            case TOK_ON_EXEC:
                assert(!"invalid i8 token");
                return cd;

            case TOK_STRP:
            case TOK_CHRP:
            case TOK_HEXP:
            case TOK_INKEYP:
            case TOK_TIMEP:
                assert(!"invalid i8 (str?)");
                return cd;

            case TOK_N_LEQ:
            case TOK_N_NEQ:
            case TOK_N_GEQ:
            case TOK_N_LE:
            case TOK_N_GE:
            case TOK_N_EQ:
            case TOK_NOT:
            case TOK_OR:
            case TOK_AND:

            case TOK_CARET:
            case TOK_STAR:
            case TOK_PLUS:
            case TOK_MINUS:
            case TOK_SLASH:

            case TOK_S_LEQ:
            case TOK_S_NEQ:
            case TOK_S_GEQ:
            case TOK_S_LE:
            case TOK_S_GE:
            case TOK_S_EQ:

            case TOK_UPLUS:
            case TOK_UMINUS:

            case TOK_USR:
            case TOK_LEN:
            case TOK_ADR:
            case TOK_FRE:
            case TOK_DPEEK:
            case TOK_ANDPER:
            case TOK_EXCLAM:
            case TOK_EXOR:
            case TOK_INSTR:
            case TOK_DEC:
            case TOK_UINSTR:
            case TOK_REG_AX:

            case TOK_REG_AL:
            case TOK_ASC:
            case TOK_PEEK:
            case TOK_PADDLE:
            case TOK_STICK:
            case TOK_PTRIG:
            case TOK_STRIG:
            case TOK_ERR:
            case TOK_ERL:

            case TOK_VAL:
            case TOK_ATN:
            case TOK_COS:
            case TOK_SIN:
            case TOK_RND:
            case TOK_EXP:
            case TOK_LOG:
            case TOK_CLOG:
            case TOK_SQR:
            case TOK_SGN:
            case TOK_ABS:
            case TOK_INT:
            case TOK_DIV:
            case TOK_FRAC:
            case TOK_TIME:
            case TOK_MOD:
            case TOK_RND_S:
            case TOK_RAND:
            case TOK_TRUNC:
        }
    }
    else
        assert(!"invalid i8 expr type");

}

static int lower_i8_expr(expr *ex, expr *src, expr *dst)
{
    code cd = CODE0;
    cd = lower_i8_expr_rec(src, dst, cd);
    return expr_replace(ex, cd);
}

static int do_lower_expr(expr *ex)
{
    assert(ex && ex->type == et_stmt);

    switch(ex->stmt)
    {
        case STMT_REM:
        case STMT_REM_:
        case STMT_EXEC_ASM:
        case STMT_GO_S:
        case STMT_LBL_S:
        case STMT_JUMP_COND:
            return 0;
        case STMT_LET:
            // See if this is a direct assignment or an indirect one
            if( expr_is_tok(ex->rgt, TOK_I_ASGN) )
            {
                if(  expr_is_tok(ex->rgt->rgt, TOK_REG_AL) ||
                     expr_is_tok(ex->rgt->rgt, TOK_REG_AX) )
                    return 0; // Already ok
                else if( expr_is_tok(ex->rgt->lft, TOK_REG_AL) )
                    return lower_i8_expr(ex, ex->rgt->rgt, ex->rgt->lft);
                else
                    return lower_i16_expr(ex, ex->rgt->rgt, ex->rgt->lft);
            }
            else if( expr_is_tok(ex->rgt, TOK_B_ASGN) )
                return lower_bool_expr(ex, ex->rgt->rgt, ex->rgt->lft);
            else if( expr_is_tok(ex->rgt, TOK_F_ASGN) )
                return lower_fp_expr(ex, ex->rgt->rgt, ex->rgt->lft);
            else if( expr_is_tok(ex->rgt, TOK_F_XSTO) )
                return lower_fp_x_expr(ex, ex->rgt->rgt, ex->rgt->lft);
            return 0;
        default:
            error("unhandled statement '%s'\n", statements[ex->stmt].stm_long);
            return 1;
    }
    return 0;
}

int lower_expr_code(expr *ex)
{
    int err = 0;
    if( !ex )
        return 0;

    // Replace definitions by the values:
    opt_replace_defs(ex);

    // Convert all statements:
    for(expr *s = ex; s != 0; s = s->lft )
        err |= do_lower_stmt(s);

    if( err )
        return err;

    // Perform constant-propagation on the resulting expressions:
    opt_constprop(ex);

    // Convert all expressions
    for(expr *s = ex; s != 0; s = s->lft )
        err |= do_lower_expr(s);

    return err;
}

