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
#include "statements.h"

typedef struct expr_struct expr;
typedef struct expr_mngr_struct expr_mngr;
typedef struct program_struct program;
typedef struct vars_struct vars;
typedef struct stmt_struct stmt;

enum enum_etype {
    et_c_number,
    et_c_hexnumber,
    et_c_string,
    et_var_number,
    et_var_string,
    et_var_array,
    et_var_label,
    et_def_string,
    et_def_number,
    et_tok,
    et_stmt,
    et_lnum,
    et_data,
    et_void
};

struct expr_struct {
    enum enum_etype type;
    expr *lft; // Left child
    expr *rgt; // Right child
    int file_line; // Line number on input file
    expr_mngr *mngr;
    double num;
    unsigned var;
    uint8_t *str;
    unsigned slen;
    enum enum_tokens tok;
    enum enum_statements stmt;
};

void expr_delete(expr *n);
expr *expr_new_void(expr_mngr *);
expr *expr_new_number(expr_mngr *, double x);
expr *expr_new_hexnumber(expr_mngr *, double x);
expr *expr_new_string(expr_mngr *, const uint8_t *str, unsigned len);
expr *expr_new_data(expr_mngr *, const uint8_t *data, unsigned len, expr *l);
expr *expr_new_bin(expr_mngr *, expr *l, expr *r, enum enum_tokens tk);
expr *expr_new_uni(expr_mngr *, expr *r, enum enum_tokens tk);
expr *expr_new_tok(expr_mngr *, enum enum_tokens tk);
expr *expr_new_stmt(expr_mngr *, expr *prev, expr *toks, enum enum_statements stmt);
expr *expr_new_lnum(expr_mngr *, expr *prev, int lnum);
expr *expr_new_var_num(expr_mngr *, int vn);
expr *expr_new_var_str(expr_mngr *, int vn);
expr *expr_new_var_array(expr_mngr *, int vn);
expr *expr_new_def_num(expr_mngr *, int dn);
expr *expr_new_def_str(expr_mngr *, int dn);
expr *expr_new_label(expr_mngr *, int vn);
int expr_to_program(expr *e, program *out);

int expr_is_label(const expr *e);
const char *expr_get_file_name(const expr *e);
program *expr_get_program(const expr *e);

int expr_get_file_line(const expr *e);
int tok_prec_level(enum enum_tokens tk);
int tok_need_parens(enum enum_tokens tk);

// Expression Manager manages the "expr" tree, allowing to free all memory
expr_mngr *expr_mngr_new(program *pgm);
void expr_mngr_set_file_line(expr_mngr *, int fline);
void expr_mngr_delete(expr_mngr *);

int expr_mngr_get_file_line(const expr_mngr *);
const char *expr_mngr_get_file_name(const expr_mngr *);
program *expr_mngr_get_program(const expr_mngr *e);

