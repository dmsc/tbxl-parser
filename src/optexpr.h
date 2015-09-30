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

#include <stdint.h>

enum enum_tokens;
typedef struct expr_struct expr;
typedef struct program_struct program;
typedef struct stmt_struct stmt;

void expr_free(expr *n);
expr *expr_new_void(program *pgm);
expr *expr_new_number(program *pgm, double x);
expr *expr_new_hexnumber(program *pgm, double x);
expr *expr_new_string(program *pgm, uint8_t *str, unsigned len);
expr *expr_new_bin(program *pgm, expr *l, expr *r, enum enum_tokens tk);
expr *expr_new_uni(program *pgm, expr *r, enum enum_tokens tk);
expr *expr_new_tok(program *pgm, enum enum_tokens tk);
expr *expr_new_var_num(program *pgm, int vn);
expr *expr_new_var_str(program *pgm, int vn);
expr *expr_new_var_array(program *pgm, int vn);
expr *expr_new_label(program *pgm, int vn);
void expr_to_tokens(expr *e, stmt *s);


