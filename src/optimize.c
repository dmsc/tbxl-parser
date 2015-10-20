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
#include "vars.h"
#include "program.h"
#include "statements.h"
#include <stdio.h>

void optimize_program(program *pgm, int level)
{
    // Convert program to expression tree
    expr *ex = pgm_get_expr(pgm);


    // Optimize:
    opt_replace_defs(ex);

    if( level & OPT_CONST_FOLD )
        opt_constprop(ex);

    if( level & OPT_COMMUTE )
        opt_commute(ex);

    if( level & OPT_LINE_NUM )
        opt_remove_line_num(ex);

    if( level & OPT_NUMBER_TOK )
        opt_convert_tok(ex);

    if( level & OPT_CONST_VARS )
        opt_replace_const(ex);

    pgm_set_expr(pgm, ex);
}
