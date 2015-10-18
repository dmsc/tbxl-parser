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
#include "optparse.h"
#include "optconst.h"
#include "optlinenum.h"
#include "optconstvar.h"
#include "vars.h"
#include "line.h"
#include "program.h"
#include "stmt.h"
#include "statements.h"
#include <stdio.h>

program *optimize_program(program *pgm, int level)
{
    program *ret = program_new(pgm_get_file_name(pgm));

    // Convert program to expression tree
    expr_mngr *mngr = expr_mngr_new(pgm);
    expr *ex = opt_parse_program(mngr);


    // Optimize:
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

    // Copy variables
    vars *v = pgm_get_vars(pgm);
    vars *vret = pgm_get_vars(ret);
    int i, nvar = vars_get_total(v);
    for(i=0; i<nvar; i++)
    {
        enum var_type t = vars_get_type(v, i);
        if( t == vtNone )
            continue;
        const char *l_name = vars_get_long_name(v, i);
        vars_new_var(vret, l_name, t, 0, -1);
    }

    expr_to_program(ex, ret);

    // Delete memory
    expr_mngr_delete(mngr);

    return ret;
}
