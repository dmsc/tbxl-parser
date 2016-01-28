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
#include "codegen.h"
#include "lowerexpr.h"
#include "stmtreplace.h"
#include "expr.h"
#include "program.h"
#include "dbg.h"
#include "lister.h"
#include "optconst.h"
#include "optlinenum.h"
#include "optrmvars.h"

// Simplify printing warnings
#define warn(...) \
    do { warn_print(expr_get_file_name(ex), expr_get_file_line(ex), __VA_ARGS__); } while(0)
#define error(...) \
    do { err_print(expr_get_file_name(ex), expr_get_file_line(ex), __VA_ARGS__); } while(0)

int codegen_generate_code(FILE *f, program *pgm)
{
    expr *ex = pgm_get_expr(pgm);
    int err = 0;

    // Perform some standard optimizations:
    // Replace defs
    err |= opt_replace_defs(ex);
    // Propagate constants
    err |= opt_constprop(ex);
    // Line number removal
    err |= opt_remove_line_num(ex);
    // Convert statemnets
    err |= replace_complex_stmt(ex);
    // Propagate constants (2)
    err |= opt_constprop(ex);
    // Remove unused variables (not really needed)
    err |= opt_remove_unused_vars(ex);

    if( err )
        return err;


    // Lower the expressions
    err |= lower_expr_code(ex);

    // List output
    err |= lister_list_program_long(f, pgm, 1);
    return err;
}

