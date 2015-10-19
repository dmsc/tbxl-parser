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

typedef struct expr_struct expr;
typedef struct vars_struct vars;
typedef struct string_buf string_buf;

string_buf *expr_print_long(const expr *e, vars *varl, int *indent, int conv_ascii);
string_buf *expr_print_short(const expr *e, vars *varl, int *skip_colon, int *no_split);
string_buf *expr_print_alone(const expr *e, vars *varl);

