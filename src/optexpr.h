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
#include "tokens.h"

typedef struct expr_struct expr;
typedef struct expr_mngr_struct expr_mngr;
typedef struct program_struct program;
typedef struct stmt_struct stmt;

enum enum_etype {
    et_c_number,
    et_c_hexnumber,
    et_c_string,
    et_var_number,
    et_var_string,
    et_var_array,
    et_var_label,
    et_tok,
    et_void
};

struct expr_struct {
    enum enum_etype type;
    expr *lft; // Left child
    expr *rgt; // Right child
    expr_mngr *mngr;
    double num;
    unsigned var;
    uint8_t *str;
    unsigned slen;
    enum enum_tokens tok;
};

void expr_delete(expr *n);
expr *expr_new_void(expr_mngr *);
expr *expr_new_number(expr_mngr *, double x);
expr *expr_new_hexnumber(expr_mngr *, double x);
expr *expr_new_string(expr_mngr *, uint8_t *str, unsigned len);
expr *expr_new_bin(expr_mngr *, expr *l, expr *r, enum enum_tokens tk);
expr *expr_new_uni(expr_mngr *, expr *r, enum enum_tokens tk);
expr *expr_new_tok(expr_mngr *, enum enum_tokens tk);
expr *expr_new_var_num(expr_mngr *, int vn);
expr *expr_new_var_str(expr_mngr *, int vn);
expr *expr_new_var_array(expr_mngr *, int vn);
expr *expr_new_label(expr_mngr *, int vn);
void expr_to_tokens(expr *e, stmt *s);


// Expression Manager manages the "expr" tree, allowing to free all memory
expr_mngr *expr_mngr_new(program *pgm);
void expr_mngr_delete(expr_mngr *);

