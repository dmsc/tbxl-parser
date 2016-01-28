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

#include "stmtreplace.h"
#include "expr.h"
#include "dbg.h"
#include "program.h"
#include "defs.h"
#include "vars.h"
#include "darray.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

// Simplify printing warnings and errors
#define warn(...) \
    do { warn_print(expr_get_file_name(ex), expr_get_file_line(ex), __VA_ARGS__); } while(0)
#define error(...) \
    do { err_print(expr_get_file_name(ex), expr_get_file_line(ex), __VA_ARGS__); } while(0)

// Generates unique variables for use in the program
typedef struct {
    int num_lbl;
    int num_flt;
    vars *vl;
} gen_var;

static gen_var *gen_var_new(vars *vl)
{
    gen_var *g = malloc(sizeof(gen_var));
    g->num_lbl = 0;
    g->num_flt = 0;
    g->vl = vl;
    return g;
}

static void gen_var_free(gen_var *g)
{
    free(g);
}

// Returns the ID of a new label
static int gen_var_lbl(gen_var *g)
{
    char name[64];
    snprintf(name, 63, "@_lbl_%d", g->num_lbl);
    g->num_lbl ++;
    int id = vars_new_var(g->vl, name, vtLabel, 0, 0);
    assert(id>=0);
    return id;
}

// Returns the ID of a new numeric variable
static int gen_var_num(gen_var *g)
{
    char name[64];
    snprintf(name, 63, "@_flt_%d", g->num_flt);
    g->num_flt ++;
    int id = vars_new_var(g->vl, name, vtFloat, 0, 0);
    assert(id>=0);
    return id;
}

// Returns the ID of a line-number label
static int gen_lnum_lbl(gen_var *g, int lnum)
{
    char name[64];
    snprintf(name, 63, "@_lin_%d", lnum);
    int id = vars_search(g->vl, name, vtLabel);
    if( id < 0 )
        id = vars_new_var(g->vl, name, vtLabel, 0, 0);
    return id;
}

// Check if expr is constant number
static int expr_is_cnum(expr *ex)
{
    return ex && (ex->type == et_c_number || ex->type == et_c_hexnumber);
}

static int check_line_number(expr *ex, const char *stmt, int max)
{
    if( !expr_is_cnum(ex) )
    {
        error("target line number is not constant in '%s'\n", stmt);
        return -1;
    }
    double ln = rint(ex->num);
    if( ln < 0 || ln > max )
    {
        error("invalid line number in '%s'\n", stmt);
        return -1;
    }
    return (int)ln;
}

static void replace_setcolor_expr(expr *ex)
{
    // SETCOLOR: COL, HUE, LUM
    expr *col = ex->rgt->lft->lft;
    expr *hue = ex->rgt->lft->rgt;
    expr *lum = ex->rgt->rgt;

    // Build an expression to calculate the color value:
    //  hue = hue & 255  (this forces 8bit integer arithmetic)
    hue = expr_new_bin(ex->mngr, hue, expr_new_number(ex->mngr, 255), TOK_ANDPER);
    //  hue = hue * 16
    hue = expr_new_bin(ex->mngr, hue, expr_new_number(ex->mngr, 16), TOK_STAR);
    //  hue = hue | lum
    hue = expr_new_bin(ex->mngr, lum, hue, TOK_EXCLAM);

    // Build an expression for the register to write:
    //  col = col & 7    (this forces integer arithmetic)
    col = expr_new_bin(ex->mngr, col, expr_new_number(ex->mngr, 7), TOK_ANDPER);
    //  col = col + 3
    col = expr_new_bin(ex->mngr, col, expr_new_number(ex->mngr, 3), TOK_PLUS);
    //  col = col & 7
    col = expr_new_bin(ex->mngr, col, expr_new_number(ex->mngr, 7), TOK_ANDPER);
    //  col = col + 705
    col = expr_new_bin(ex->mngr, col, expr_new_number(ex->mngr, 705), TOK_PLUS);

    // Build "POKE" expression
    ex->rgt = expr_new_bin(ex->mngr, col, hue, TOK_COMMA);
    ex->stmt = STMT_POKE;
}

static void replace_sound_expr(expr *ex)
{
    if( !ex->rgt )
        return;  // No parameters, keep

    // SOUND: CHN, FREQ, DIST, VOL
    expr *chn = ex->rgt->lft->lft->lft;
    expr *frq = ex->rgt->lft->lft->rgt;
    expr *dst = ex->rgt->lft->rgt;
    expr *vol = ex->rgt->rgt;

    // Only replace if chn is a constant value
    if( !expr_is_cnum(chn) )
        return;

    // Build an expression to calculate the AUDC value:
    //  dst = dst & 255  (this forces 8bit integer arithmetic)
    dst = expr_new_bin(ex->mngr, dst, expr_new_number(ex->mngr, 255), TOK_ANDPER);
    //  dst = dst * 16
    dst = expr_new_bin(ex->mngr, dst, expr_new_number(ex->mngr, 16), TOK_STAR);
    //  dst = dst | vol
    dst = expr_new_bin(ex->mngr, dst, vol, TOK_EXCLAM);

    // Build an expression for the channel registers:
    //  chn = chn & 3    (this forces 8bit integer arithmetic)
    chn = expr_new_bin(ex->mngr, chn, expr_new_number(ex->mngr, 3), TOK_ANDPER);
    //  chn = chn * 2
    chn = expr_new_bin(ex->mngr, chn, expr_new_number(ex->mngr, 2), TOK_STAR);
    //  chn = chn + $D200
    chn = expr_new_bin(ex->mngr, chn, expr_new_number(ex->mngr, 0xD200), TOK_PLUS);
    //  audc = chn + 1
    expr *audc = expr_new_bin(ex->mngr, chn, expr_new_number(ex->mngr, 1), TOK_PLUS);

    // Build 3 "POKE" expressions:
    expr *old_next = ex->lft;
    // POKE SKCTL, $03
    ex->rgt = expr_new_bin(ex->mngr, expr_new_number(ex->mngr, 0xD20F),
                           expr_new_number(ex->mngr, 0x03), TOK_COMMA);
    ex->stmt = STMT_POKE;
    // POKE AUDF*, frq
    ex = expr_new_stmt(ex->mngr, ex, expr_new_bin(ex->mngr, chn, frq, TOK_COMMA), STMT_POKE);
    // POKE AUDC*, frq
    ex = expr_new_stmt(ex->mngr, ex, expr_new_bin(ex->mngr, audc, dst, TOK_COMMA), STMT_POKE);
    // Link
    ex->lft = old_next;
}

static void replace_cls_expr(expr *ex)
{
    // CLS character number:
    if( ex->rgt )
    {
        // replace CLS #* with PUT #*, 125
        ex->rgt = expr_new_bin(ex->mngr, ex->rgt, expr_new_number(ex->mngr, 125), TOK_COMMA);
        ex->stmt = STMT_PUT;
    }
    else
    {
        // replace CLS with PUT 125
        ex->rgt = expr_new_number(ex->mngr, 125);
        ex->stmt = STMT_PUT;
    }
}

static void set_label(expr *ex, int ln)
{
    ex->stmt = STMT_LBL_S;
    ex->rgt = expr_new_label(ex->mngr, ln);
}

static void set_goto(expr *ex, int ln)
{
    ex->stmt = STMT_GO_S;
    ex->rgt = expr_new_label(ex->mngr, ln);
}

static void replace_if_expr(expr *ex, int l1)
{
    // Replace IF <ex>    ->   IF NOT <ex> THEN #l1
    // Negate the condition:
    expr *not = expr_new_uni(ex->mngr, ex->rgt, TOK_NOT);
    // Build an IF/THEN expression with the new condition:
    ex->stmt = STMT_IF_NUMBER;
    ex->rgt = expr_new_bin(ex->mngr, not, expr_new_label(ex->mngr, l1), TOK_THEN);
}

static void replace_else_expr(expr *ex, int l1, int l2)
{
    // Replace ELSE       ->    GO#l2
    //                          #l1
    expr *old_next = ex->lft;
    ex->stmt = STMT_GO_S;
    ex->rgt  = expr_new_label(ex->mngr, l2);
    ex = expr_new_stmt(ex->mngr, ex, expr_new_label(ex->mngr, l1), STMT_LBL_S);
    ex->lft = old_next;
}

static void replace_until_expr(expr *ex, int l1, int l2)
{
    // Replace UNTIL <ex>  ->   IF NOT <ex> THEN #l1
    //                          #l1
    expr *old_next = ex->lft;
    // Negate the condition:
    expr *not = expr_new_uni(ex->mngr, ex->rgt, TOK_NOT);
    // Build an IF/THEN expression with the condition:
    ex->stmt = STMT_IF_NUMBER;
    ex->rgt = expr_new_bin(ex->mngr, not, expr_new_label(ex->mngr, l1), TOK_THEN);
    ex = expr_new_stmt(ex->mngr, ex, expr_new_label(ex->mngr, l2), STMT_LBL_S);
    ex->lft = old_next;
}

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

static void replace_for_expr(expr *ex, int var_i, int var_end, int var_step,
                             expr *ex_start, expr *ex_end, expr *ex_step,
                             int l1, int l2, int l3)
{
    // Replace:
    //       FOR <var_i> = <ex_start> TO <ex_end> STEP <ex_step>
    //  ->
    //       <var_i>    = <ex_start>
    //       <var_end>  = <ex_end>
    //       <var_step> = <ex_step>
    //       IF (*F) THEN #L2
    //       #L1
    //
    // NOTE: *F flag is not readable from standard TurboBasicXL code, so
    //       it is replaced by an ASM label, this breaks the abstraction
    //       but works for the compiler.
    expr *f_flag = get_asm_label(ex, "bas_for_flag");

    expr *old_next = ex->lft;

    ex->stmt = STMT_LET;
    ex->rgt = expr_new_bin(ex->mngr, expr_new_var_num(ex->mngr, var_i), ex_start, TOK_F_ASGN);
    ex = expr_new_stmt(ex->mngr, ex,
            expr_new_bin(ex->mngr, expr_new_var_num(ex->mngr, var_end), ex_end, TOK_F_ASGN),
            STMT_LET);
    ex = expr_new_stmt(ex->mngr, ex,
            expr_new_bin(ex->mngr, expr_new_var_num(ex->mngr, var_step), ex_step, TOK_F_ASGN),
            STMT_LET);
    ex = expr_new_stmt(ex->mngr, ex,
            expr_new_bin(ex->mngr, f_flag, expr_new_label(ex->mngr, l2), TOK_THEN),
            STMT_IF_NUMBER);
    ex = expr_new_stmt(ex->mngr, ex, expr_new_label(ex->mngr, l1), STMT_LBL_S);
    ex->lft = old_next;
}

static void replace_next_expr(expr *ex, int var_i, int var_end, int var_step,
                              int l1, int l2, int l3)
{
    // Replace:
    //       NEXR <var_i>
    //  ->
    //       <var_i>    = <var_i> + <var_step>
    //       #L2
    //       IF (<var_step> < 0)  AND (<var_i> >= <var_end>) OR
    //          (<var_step> >= 0) AND (<var_i> <= <var_end>)   THEN #L1
    //       #L3
    expr *old_next = ex->lft;

    // Build comparison
    expr *c1 = expr_new_bin(ex->mngr, expr_new_var_num(ex->mngr, var_step),
                            expr_new_number(ex->mngr, 0), TOK_N_LE);
    expr *c2 = expr_new_bin(ex->mngr, expr_new_var_num(ex->mngr, var_step),
                            expr_new_number(ex->mngr, 0), TOK_N_GEQ);
    expr *c3 = expr_new_bin(ex->mngr, expr_new_var_num(ex->mngr, var_i),
                            expr_new_var_num(ex->mngr, var_end), TOK_N_GEQ);
    expr *c4 = expr_new_bin(ex->mngr, expr_new_var_num(ex->mngr, var_i),
                            expr_new_var_num(ex->mngr, var_end), TOK_N_LEQ);
    expr *comp = expr_new_bin(ex->mngr, expr_new_bin(ex->mngr, c1, c3, TOK_AND),
                              expr_new_bin(ex->mngr, c2, c4, TOK_AND), TOK_OR);

    // Build statements
    ex->stmt = STMT_LET;
    ex->rgt = expr_new_bin(ex->mngr, expr_new_var_num(ex->mngr, var_i),
                           expr_new_bin(ex->mngr, expr_new_var_num(ex->mngr, var_i),
                           expr_new_var_num(ex->mngr, var_step), TOK_PLUS), TOK_F_ASGN);
    ex = expr_new_stmt(ex->mngr, ex, expr_new_label(ex->mngr, l2), STMT_LBL_S);
    ex = expr_new_stmt(ex->mngr, ex,
                       expr_new_bin(ex->mngr, comp, expr_new_label(ex->mngr, l1), TOK_THEN),
                       STMT_IF_NUMBER);
    ex = expr_new_stmt(ex->mngr, ex, expr_new_label(ex->mngr, l3), STMT_LBL_S);
    ex->lft = old_next;
}

static void replace_while_expr(expr *ex, int l1, int l2)
{
    // Replace WHILE      ->    GO#l2
    //                          #l1
    expr *old_next = ex->lft;
    ex->stmt = STMT_GO_S;
    ex->rgt  = expr_new_label(ex->mngr, l2);
    ex = expr_new_stmt(ex->mngr, ex, expr_new_label(ex->mngr, l1), STMT_LBL_S);
    ex->lft = old_next;
}

static void replace_loop_expr(expr *ex, int l1, int l2)
{
    // Replace LOOP       ->    GO#l2
    //                          #l1
    expr *old_next = ex->lft;
    ex->stmt = STMT_GO_S;
    ex->rgt  = expr_new_label(ex->mngr, l2);
    ex = expr_new_stmt(ex->mngr, ex, expr_new_label(ex->mngr, l1), STMT_LBL_S);
    ex->lft = old_next;
}

static void replace_wend_expr(expr *ex, expr *cond, int l1, int l2, int l3)
{
    // Replace WEND       ->    #l2
    //                          IF <cond> THEN #l1
    //                          #l3
    expr *old_next = ex->lft;
    ex->stmt = STMT_LBL_S;
    ex->rgt  = expr_new_label(ex->mngr, l2);
    ex = expr_new_stmt(ex->mngr, ex,
                       expr_new_bin(ex->mngr, cond, expr_new_label(ex->mngr, l1), TOK_THEN),
                       STMT_IF_NUMBER);
    ex = expr_new_stmt(ex->mngr, ex, expr_new_label(ex->mngr, l3), STMT_LBL_S);
    ex->lft = old_next;
}

static void replace_dim_expr(expr *ex)
{
    expr *toks = ex->rgt;
    expr *old_next = ex->lft;
    // Don't use "COM"
    ex->stmt = STMT_DIM;

    // Replace each variable with its own DIM
    while(toks && toks->type == et_tok && toks->tok == TOK_COMMA)
    {
        expr *var = toks->rgt;
        toks = toks->lft;
        ex->rgt = var;
        ex = expr_new_stmt(ex->mngr, ex, toks, STMT_DIM);
    }
    ex->lft = old_next;
}

static int replace_line_nums(expr *ex, gen_var *gv)
{
    if( !ex )
        return 0;
    if( ex->type == et_tok && ex->tok == TOK_COMMA )
        return replace_line_nums(ex->lft, gv) | replace_line_nums(ex->rgt, gv);

    int ln = check_line_number(ex, "ON/GOTO/GOSUB", 32767);
    if( ln < 0 )
        return 1;
    ex->type = et_tok;
    ex->tok  = TOK_SHARP;
    ex->lft  = 0;
    ex->rgt  = expr_new_label(ex->mngr, gen_lnum_lbl(gv, ln));
    return 0;
}

static int replace_on_expr(expr *ex, gen_var *gv)
{
    // Don't replace if already ok
    if( ex->rgt->type != et_tok || ex->rgt->tok == TOK_ON_GOSHARP ||
        ex->rgt->tok == TOK_ON_EXEC )
        return 0;
    // Change GOSUB -> EXEC and GOTO -> GO#
    if( ex->rgt->tok == TOK_ON_GOSUB )
        ex->rgt->tok = TOK_ON_EXEC;
    else
        ex->rgt->tok = TOK_ON_GOSHARP;
    // Create labels for each number
    return replace_line_nums(ex->rgt->rgt, gv);
}

static int do_replace_stmt(expr *ex, gen_var *gvar,
                           enum enum_statements expected, expr **found,
                           int exit_label)
{
    expr *nxt;

    for( ; ex; ex = nxt )
    {
        assert(ex->type == et_stmt || ex->type == et_lnum);
        nxt = ex->lft;

        // Convert line numbers to labels
        if( ex->type == et_lnum )
        {
            ex->type = et_stmt;
            ex->stmt = STMT_LBL_S;
            ex->rgt = expr_new_label(ex->mngr, gen_lnum_lbl(gvar, ex->num));
            continue;
        }

        switch(ex->stmt)
        {
            case STMT_BAS_ERROR:
            case STMT_CLOAD:
            case STMT_CLR:
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
            case STMT_BGET:
            case STMT_BLOAD:
            case STMT_BPUT:
            case STMT_BRUN:
            case STMT_BYE:
            case STMT_CIRCLE:
            case STMT_CLOSE:
            case STMT_COLOR:
            case STMT_DATA:
            case STMT_DEG:
            case STMT_DELETE:
            case STMT_DIR:
            case STMT_DOS:
            case STMT_DPOKE:
            case STMT_DRAWTO:
            case STMT_DSOUND:
            case STMT_END:
            case STMT_FCOLOR:
            case STMT_FILLTO:
            case STMT_F_B:
            case STMT_F_F:
            case STMT_GET:
            case STMT_GRAPHICS:
            case STMT_INPUT:
            case STMT_LET:
            case STMT_LOCATE:
            case STMT_LOCK:
            case STMT_LPRINT:
            case STMT_MOVE:
            case STMT_NEW:
            case STMT_NOTE:
            case STMT_N_MOVE:
            case STMT_OPEN:
            case STMT_PAINT:
            case STMT_PAUSE:
            case STMT_PLOT:
            case STMT_POINT:
            case STMT_POKE:
            case STMT_POSITION:
            case STMT_PRINT:
            case STMT_PRINT_:
            case STMT_PUT:
            case STMT_P_GET:
            case STMT_P_PUT:
            case STMT_RAD:
            case STMT_READ:
            case STMT_REM:
            case STMT_REM_:
            case STMT_RENAME:
            case STMT_STATUS:
            case STMT_STOP:
            case STMT_TEXT:
            case STMT_TIME_S:
            case STMT_UNLOCK:
            case STMT_XIO:

            case STMT_LBL_S:
            case STMT_RETURN:
            case STMT_EXEC:
            case STMT_GO_S:

                // Those are not possible here:
            case STMT_EXEC_ASM:
            case STMT_JUMP_COND:
            case STMT_EXEC_PAR:
            case STMT_IF:
            case STMT_PROC_VAR:
                // Nothing to do
                break;

            case STMT_TRAP:
                if( !ex->rgt || (ex->rgt->type == et_tok && ex->rgt->tok == TOK_SHARP) )
                    break;
                else
                {
                    int ln = check_line_number(ex->rgt, "TRAP", 65535);
                    if( ln < 0 )
                        return 1;
                    else if( ln > 32767 )
                        ex->rgt = 0;
                    else
                        ex->rgt = expr_new_uni(ex->mngr,
                                expr_new_label(ex->mngr, gen_lnum_lbl(gvar, ln)),
                                TOK_SHARP);
                }
                break;

            case STMT_RESTORE:
                if( !ex->rgt || (ex->rgt->type == et_tok && ex->rgt->tok == TOK_SHARP) )
                    break;
                else
                {
                    int ln = check_line_number(ex->rgt, "RESTORE", 32767);
                    if( ln < 0 )
                        return 1;
                    ex->rgt = expr_new_uni(ex->mngr,
                                           expr_new_label(ex->mngr, gen_lnum_lbl(gvar, ln)),
                                           TOK_SHARP);
                }
                break;

            case STMT_ON:
                replace_on_expr(ex, gvar);
                break;

            case STMT_POP:
                warn("ignoring POP statement, not needed.\n");
                ex->stmt = STMT_REM_;
                ex->rgt = 0;
                break;

            case STMT_PROC:
                ex->stmt = STMT_LBL_S;
                break;

            case STMT_ENDPROC:
                ex->stmt = STMT_RETURN;
                break;

            case STMT_GOTO:
            case STMT_GO_TO:
                {
                    int ln = check_line_number(ex->rgt, "GOTO", 32767);
                    if( ln < 0 )
                        return 1;
                    ex->stmt = STMT_GO_S;
                    ex->rgt = expr_new_label(ex->mngr, gen_lnum_lbl(gvar, ln));
                }
                break;

            case STMT_IF_NUMBER:
                {
                    int ln = check_line_number(ex->rgt->rgt, "IF/THEN", 32767);
                    if( ln < 0 )
                        return 1;
                    ex->rgt->rgt = expr_new_label(ex->mngr, gen_lnum_lbl(gvar, ln));
                }
                break;

            case STMT_GOSUB:
                {
                    int ln = check_line_number(ex->rgt, "GOSUB", 32767);
                    if( ln < 0 )
                        return 1;
                    ex->stmt = STMT_EXEC;
                    ex->rgt = expr_new_label(ex->mngr, gen_lnum_lbl(gvar, ln));
                }
                break;

            case STMT_LET_INV:
                ex->stmt = STMT_LET;
                break;

            case STMT_EXIT:
                if( exit_label < 0 )
                {
                    error("EXIT outside any FOR/WHILE/REPEAT/LOOP\n");
                    return 1;
                }
                if( ex->rgt )
                    warn("ignoring EXIT parameter for compatibility with TurboBasic XL\n");
                // Goto exit_label
                set_goto(ex, exit_label);
                break;


            case STMT_CLS:
                // Replace with "PUT 155" or "PUT #*, 155"
                replace_cls_expr(ex);
                break;

            case STMT_COM:
            case STMT_DIM:
                // Replace with one variable DIMs
                replace_dim_expr(ex);
                break;

            case STMT_SETCOLOR:
                // Replace with one POKE to the correct address
                replace_setcolor_expr(ex);
                break;

            case STMT_SOUND:
                // Replace with 3 POKEs if possible:
                replace_sound_expr(ex);
                break;

            case STMT_DO:
                {
                    int lbl1_id = gen_var_lbl(gvar);
                    int lbl2_id = gen_var_lbl(gvar);
                    set_label(ex, lbl1_id);
                    // Search LOOP
                    expr *end = 0;
                    if( do_replace_stmt(nxt, gvar, STMT_LOOP, &end, lbl2_id) || !end )
                    {
                        error("'DO' without corresponding 'LOOP'.\n");
                        return 1;
                    }
                    ex = end;
                    nxt = end->lft;
                    replace_loop_expr(ex, lbl2_id, lbl1_id);
                }
                break;

            case STMT_REPEAT:
                {
                    int lbl1_id = gen_var_lbl(gvar);
                    int lbl2_id = gen_var_lbl(gvar);
                    set_label(ex, lbl1_id);
                    // Search UNTIL
                    expr *end = 0;
                    if( do_replace_stmt(nxt, gvar, STMT_UNTIL, &end, lbl2_id) || !end )
                    {
                        error("'REPEAT' without corresponding 'UNTIL'.\n");
                        return 1;
                    }
                    ex = end;
                    nxt = end->lft;
                    replace_until_expr(ex, lbl1_id, lbl2_id);
                }
                break;

            case STMT_WHILE:
                {
                    int lbl1_id = gen_var_lbl(gvar);
                    int lbl2_id = gen_var_lbl(gvar);
                    int lbl3_id = gen_var_lbl(gvar);
                    expr *cond = ex->rgt;
                    replace_while_expr(ex, lbl1_id, lbl2_id);
                    // Search WEND
                    expr *end = 0;
                    if( do_replace_stmt(nxt, gvar, STMT_WEND, &end, lbl3_id) || !end )
                    {
                        error("'WHILE' without corresponding 'WEND'.\n");
                        return 1;
                    }
                    ex = end;
                    nxt = end->lft;
                    replace_wend_expr(ex, cond, lbl1_id, lbl2_id, lbl3_id);
                }
                break;

            case STMT_FOR:
                {
                    int lbl1_id = gen_var_lbl(gvar);
                    int lbl2_id = gen_var_lbl(gvar);
                    int lbl3_id = gen_var_lbl(gvar);
                    int var_end = gen_var_num(gvar);
                    int var_stp = gen_var_num(gvar);
                    expr *ex_step;
                    expr *ex_for;
                    if( ex->rgt->tok == TOK_STEP )
                    {
                        ex_for = ex->rgt->lft;
                        ex_step = ex->rgt->rgt;
                    }
                    else
                    {
                        ex_for = ex->rgt;
                        ex_step = expr_new_number(ex->mngr, 1);
                    }
                    int var_itr    = ex_for->lft->lft->var;
                    expr *ex_start = ex_for->lft->rgt;
                    expr *ex_end   = ex_for->rgt;

                    replace_for_expr(ex, var_itr, var_end, var_stp,
                                     ex_start, ex_end, ex_step,
                                     lbl1_id, lbl2_id, lbl3_id);
                    // Search NEXT
                    expr *end = 0;
                    if( do_replace_stmt(nxt, gvar, STMT_NEXT, &end, lbl3_id) || !end )
                    {
                        error("'FOR' without corresponding 'NEXT'.\n");
                        return 1;
                    }
                    ex = end;
                    nxt = end->lft;
                    if( ex->rgt->var != var_itr )
                    {
                        error("'NEXT' does not corresponds with nearest 'FOR'\n");
                        return 1;
                    }
                    replace_next_expr(ex, var_itr, var_end, var_stp,
                                      lbl1_id, lbl2_id, lbl3_id);
                }
                break;

            case STMT_IF_MULTILINE:
                {
                    int lbl1_id = gen_var_lbl(gvar);
                    replace_if_expr(ex, lbl1_id);
                    // Search ENDIF
                    expr *end = 0;
                    if( do_replace_stmt(nxt, gvar, STMT_ELSE, &end, exit_label) || !end )
                    {
                        error("'IF' without corresponding 'ELSE/ENDIF'.\n");
                        return 1;
                    }
                    ex = end;
                    nxt = end->lft;
                    if( ex->stmt == STMT_ENDIF )
                    {
                        set_label(ex, lbl1_id);
                        break;
                    }

                    // Complete ELSE/ENDIF
                    int lbl2_id = gen_var_lbl(gvar);
                    replace_else_expr(end, lbl1_id, lbl2_id);
                    end = 0;
                    if( do_replace_stmt(nxt, gvar, STMT_ENDIF, &end, exit_label) || !end )
                    {
                        error("'ELSE' without corresponding 'ENDIF'.\n");
                        return 1;
                    }
                    ex = end;
                    nxt = end->lft;
                    set_label(ex, lbl2_id);
                }
                break;

            case STMT_IF_THEN:
                {
                    int lbl_id = gen_var_lbl(gvar);
                    replace_if_expr(ex, lbl_id);
                    // Search ENDIF_INVISIBLE
                    expr *end = 0;
                    if( do_replace_stmt(nxt, gvar, STMT_ENDIF_INVISIBLE, &end, exit_label) || !end )
                    {
                        error("unterminated 'IF'.\n");
                        return 1;
                    }
                    ex = end;
                    nxt = end->lft;
                    set_label(ex, lbl_id);
                }
                break;

                // End of loop/if statements:
            case STMT_LOOP:
            case STMT_NEXT:
            case STMT_WEND:
            case STMT_UNTIL:
            case STMT_ELSE:
            case STMT_ENDIF:
            case STMT_ENDIF_INVISIBLE:
                if( found &&
                    (expected == ex->stmt || (ex->stmt == STMT_ENDIF && expected == STMT_ELSE)))
                {
                    *found = ex;
                    return 0;
                }
                if( expected >= 0 && expected < sizeof(statements)/sizeof(statements[0]) )
                    error("nesting error, expected '%s' instead of '%s'.\n",
                          statements[expected].stm_long, statements[ex->stmt].stm_long);
                else
                    error("nesting error, dangling unexpected '%s'.\n",
                          statements[ex->stmt].stm_long);
                return 1;
        }

    }
    return 0;
}

int replace_complex_stmt(expr *ex)
{
    if( !ex )
        return 0;

    gen_var *gvar = gen_var_new(pgm_get_vars(expr_get_program(ex)));
    // Convert all statements:
    int err = do_replace_stmt(ex, gvar, -1, 0, -1);

    gen_var_free(gvar);
    return err;
}

