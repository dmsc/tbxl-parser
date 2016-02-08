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
typedef struct string_buf string_buf;

string_buf *expr_get_bas(const expr *e, int *end_colon, int *no_split);
int expr_get_bas_len(const expr *e);
// Returns the maximum length of a tokenized line with this statement as
// the last one, this is to fix TurboBasic XL interpreter bugs
unsigned expr_get_bas_maxlen(const expr *e);

