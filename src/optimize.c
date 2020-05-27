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
#include "optrmvars.h"
#include "vars.h"
#include "program.h"
#include "statements.h"
#include <stdio.h>
#include <string.h>

enum optimize_levels optimize_option(const char *opt)
{
    if( 0 == strcmp(opt, "const_folding") )
        return OPT_CONST_FOLD;
    else if( 0 == strcmp(opt, "convert_percent") )
        return OPT_NUMBER_TOK;
    else if( 0 == strcmp(opt, "commute") )
        return OPT_COMMUTE;
    else if( 0 == strcmp(opt, "line_numbers") )
        return OPT_LINE_NUM;
    else if( 0 == strcmp(opt, "const_replace") )
        return OPT_CONST_VARS;
    else if( 0 == strcmp(opt, "fixed_vars") )
        return OPT_FIXED_VARS;
    else
        return 0;
}

enum optimize_levels optimize_all(void)
{
    return OPT_CONST_FOLD | OPT_NUMBER_TOK | OPT_COMMUTE | OPT_LINE_NUM | OPT_CONST_VARS;
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

    pgm_set_expr(pgm, ex);
    return err;
}
