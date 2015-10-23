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

#include "convertbas.h"
#include "procparams.h"
#include "expr.h"
#include "program.h"

int convert_to_turbobas(program *p)
{
    int err = 0;
    expr *ex = pgm_get_expr(p);

    // Converts PROC/EXEC with parameters and local variables to standard PROC/EXEC.
    err |= convert_proc_exec(ex);

    return err;
}
