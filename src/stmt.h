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

enum enum_statements;
enum enum_tokens;
typedef struct vars_struct vars;
typedef struct string_buf string_buf;

// Represents one statement
typedef struct stmt_struct stmt;

stmt *stmt_new(enum enum_statements sn);
void stmt_delete(stmt *s);

// Build statements
void stmt_add_token(stmt *s, enum enum_tokens tok);
void stmt_add_var(stmt *s, int id);
void stmt_add_string(stmt *s, const char *data, unsigned len);
int  stmt_add_extended_string(stmt *s, const char *data, unsigned len);
void stmt_add_number(stmt *s, double d);
void stmt_add_hex_number(stmt *s, double d);
void stmt_add_data(stmt *s, const char *txt, unsigned len);
void stmt_add_comment(stmt *s, const char *txt, unsigned len);

// Returns 1 if statement is "valid"
int stmt_is_valid(const stmt *s);
// Returns 1 if statement is REM or ERROR
int stmt_can_skip(const stmt *s);
// Returns 1 if statement is textual (REM, ERROR or DATA)
int stmt_is_text(const stmt *s);
// Returns 1 if the statement is a label so it needs to be at the start of a line
// (those are #label and PROC)
int stmt_is_label(const stmt *s);

// Prints the statement to a newly allocated string
string_buf *stmt_print_long(stmt *s, vars *varl, int *indent, int *skip_colon, int conv_ascii);
string_buf *stmt_print_short(stmt *s, vars *varl, int *skip_colon);
string_buf *stmt_print_alone(stmt *s, vars *varl);

// Returns the binary "BAS" representation of the statement
string_buf *stmt_get_bas(stmt *s, vars *varl, int *end_colon);

