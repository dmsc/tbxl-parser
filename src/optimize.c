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

#include "optimize.h"
#include "expr.h"
#include "optconst.h"
#include "optlinenum.h"
#include "optconstvar.h"
#include "optifgoto.h"
#include "optrmvars.h"
#include "vars.h"
#include "program.h"
#include "statements.h"
#include <stdio.h>
#include <string.h>

static struct optimization_options {
    enum optimize_levels lvl;
    const char *tag;
    const char *desc;
} opts[] = {
    { OPT_CONST_FOLD, "const_folding",   "Replace operations on constants with result" },
    { OPT_NUMBER_TOK, "convert_percent", "Replace small constants with %0 to %3 (TBXL only)" },
    { OPT_COMMUTE,    "commute",         "Swap operands for less size and more speed" },
    { OPT_LINE_NUM,   "line_numbers",    "Remove all unused line numbers" },
    { OPT_CONST_VARS, "const_replace",   "Replace repeated constants with variables" },
    { OPT_FIXED_VARS, "fixed_vars",      "Remove variables with constant values" },
    { OPT_THEN_GOTO,  "then_goto",       "Convert THEN GOTO to THEN alone" },
    { OPT_IF_GOTO,    "if_goto",         "Also convert IF/GOTO/ENDIF to IF/THEN alone (TBXL)" },
    { 0, 0, 0 }
};

enum optimize_levels optimize_option(const char *opt)
{
    struct optimization_options *o;
    for(o = &opts[0]; o->lvl; o++)
        if( 0 == strcasecmp(opt, o->tag) )
            return o->lvl;
    return 0;
}

enum optimize_levels optimize_all(void)
{
    return OPT_CONST_FOLD | OPT_NUMBER_TOK | OPT_COMMUTE |
           OPT_LINE_NUM | OPT_CONST_VARS | OPT_THEN_GOTO;
}

void optimize_list_options(void)
{
    struct optimization_options *o;
    enum optimize_levels def = optimize_all();

    fprintf(stderr, "List of optimization options:\n");
    for(o = &opts[0]; o->lvl; o++)
        fprintf(stderr, "\t%-16s %c  %s\n",
                o->tag, 0 == (o->lvl & def) ? ' ' : '*', o->desc );
    fprintf(stderr, "\nOptions with '*' are enabled with the '-O' option alone.\n");
}

int optimize_program(program *pgm, int level)
{
    // Convert program to expression tree
    expr *ex = pgm_get_expr(pgm);
    int err = 0;


    // Optimize:
    err = opt_replace_defs(ex);

    if( level & OPT_CONST_FOLD )
        err |= opt_constprop(ex);

    if( level & OPT_COMMUTE )
        err |= opt_commute(ex);

    if( level & OPT_FIXED_VARS )
    {
        err |= opt_replace_fixed_vars(ex);
        // After fixed variable removal, redo constant propagation and try again
        if( level & OPT_CONST_FOLD )
        {
            err |= opt_constprop(ex);
            err |= opt_replace_fixed_vars(ex);
        }
    }

    if( level & OPT_LINE_NUM )
        err |= opt_remove_line_num(ex);

    if( level & OPT_NUMBER_TOK )
        err |= opt_convert_tok(ex);

    err |= opt_remove_unused_vars(ex);

    if( level & OPT_CONST_VARS )
        err |= opt_replace_const(ex);

    if( level & OPT_IF_GOTO || level & OPT_THEN_GOTO )
        err |= opt_convert_then_goto(ex, level & OPT_IF_GOTO);

    pgm_set_expr(pgm, ex);
    return err;
}
