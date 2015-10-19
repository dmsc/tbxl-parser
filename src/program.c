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
#include "program.h"
#include "expr.h"
#include "vars.h"
#include "defs.h"

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

struct program_struct {
    vars *variables; // Program variables
    defs *defines;   // Program constant defines
    expr_mngr *mngr; // Expression manager.
    expr *expr;      // Tree representation of the program
    char *file_name; // Input file name
};

program *program_new(const char *file_name)
{
    program *p;
    p = malloc(sizeof(program));
    p->variables = vars_new();
    p->defines   = defs_new();
    p->file_name = strdup(file_name);
    p->expr = 0;
    p->mngr = expr_mngr_new(p);
    return p;
}

void program_delete(program *p)
{
    vars_delete( p->variables );
    defs_delete( p->defines );
    expr_mngr_delete( p->mngr );
    free( p->file_name );
    free( p );
}

vars *pgm_get_vars(program *p)
{
    return p->variables;
}

defs *pgm_get_defs(program *p)
{
    return p->defines;
}

expr *pgm_get_expr(program *p)
{
    return p->expr;
}

void pgm_set_expr(program *p, expr *e)
{
    p->expr = e;
}

expr_mngr *pgm_get_expr_mngr(program *p)
{
    return p->mngr;
}

const char *pgm_get_file_name(program *p)
{
    return p->file_name;
}
