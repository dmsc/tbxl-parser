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
#pragma once

typedef struct program_struct program;
typedef struct expr_struct expr;
typedef struct expr_mngr_struct expr_mngr;
typedef struct vars_struct vars;
typedef struct defs_struct defs;

program *program_new(const char *fname);
void program_delete(program *p);

void pgm_set_expr(program *p, expr *e);
vars *pgm_get_vars(program *p);
defs *pgm_get_defs(program *p);
expr *pgm_get_expr(program *p);
expr_mngr *pgm_get_expr_mngr(program *p);
const char *pgm_get_file_name(program *p);
