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

#include "parser.h"
#include "optimize.h"

// Called from PEG
enum enum_statements;
enum enum_tokens;
enum var_type;

void add_comment(const char *, int);
void add_data_stmt(const char *, int);
void add_force_line(void);
void add_linenum(double);
void add_number(double);
void add_hex_number(double);
void add_string(void);
void add_token(enum enum_tokens);
void add_stmt(enum enum_statements);
void add_ident(const char *, enum var_type);
void print_error(const char *, const char *);
void add_strdef_val(const char *);
void add_numdef_val(const char *);

// Converts strings constants to binary data and store
void push_string_const(const char *data, unsigned len);
void push_extended_string(const char *data, unsigned len);

// Used to add binary includes
void add_definition(const char *var_name);
void add_incbin_file(const char *bin_file_name);
void set_numdef_value(double);

// Used to keep current input file line number
void inc_file_line(void);

void parse_init(const char *fname);
int get_parse_errors(void);
